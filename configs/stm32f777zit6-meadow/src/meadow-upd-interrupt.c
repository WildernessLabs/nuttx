/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\meadow-upd-interrupt.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
 *   Copyright (C) 2017 Gregory Nutt. All rights reserved.
 *   Copyright (C) 2017 Alan Carvalho de Assis. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>

#include <nuttx/config.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <arch/board/board.h>
#include <nuttx/mqueue.h>
#include <nuttx/signal.h>
#include <nuttx/drivers/pwm.h>
#include <nuttx/spi/spi.h>

#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "chip.h"
#include "fcntl.h"
#include "stm32_pwm.h"
#include "stm32_i2c.h"
#include "stm32f777zit6-meadow.h"
#include "stm32_spi.h"

#include <dirent.h>

#include <sys/ioctl.h>

#include <nuttx/timers/timer.h>
#include <sys/ioctl.h>
#include "stm32_tim.h"
#include <nuttx/clock.h>    // for testing

#include "meadow-upd.h"
/****************************************************************************
 * Private Types
 ****************************************************************************/

#define HCOM_ONBOARD_D05_PIN_OUTPUT  (GPIO_OUTPUT | GPIO_PORTC | GPIO_PIN7 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define HCOM_ONBOARD_D06_PIN_OUTPUT  (GPIO_OUTPUT | GPIO_PORTB | GPIO_PIN0 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define HCOM_ONBOARD_D07_PIN_OUTPUT  (GPIO_OUTPUT | GPIO_PORTB | GPIO_PIN7 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define HCOM_ONBOARD_D08_PIN_OUTPUT  (GPIO_OUTPUT | GPIO_PORTB | GPIO_PIN6 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)

enum updInterruptProcState_e
{
  updipstate_uncfg,       // not configured
  //updipstate_cfg,         // Configured, waiting on first interrupt
  updipstate_wait_gpio_isr,   // waiting for an interrupt from a gpio
  updipstate_mon_glitch,     // gpio is being monitored for glitch
  //updipstate_post_glitch,        // finished glitch waiting for debounce
  updipstate_mon_debounce,   // gpio is being monitored for debounce
};

enum GpioInterruptMode_e
{
  gldbintmode_none,
  gldbintmode_rising,
  gldbintmode_falling,
  gldbintmode_both
};

enum GlitchAndDebouceReturnValues_e
{
  gadrv_keepwaiting,
  gadrv_glitchdetected,
  gadrv_validtransitiondetected,
  gadrv_break,
  gadrv_continue
};

// The following struct stores the informtion needed for debounce
struct interruptPinMap_s
{
  // Represents the CPU Pin identifier (e.g. PD9, D=3 so 39)
  const uint8_t PinId;
  // Address of the "Input Data Register" is that has the state bits
  const uint32_t IDRAddress;
  // CurrentProcessState - tracks the current processing state for this GPIO
  uint8_t CurrentProcessState;  // 1 = high & 0 = low read via IDRAddress
  // Contains the interrupt mode uses desires: None = 0, Rising = 1, Falling = 2 and Both = 3;
  uint8_t GpioInterruptMode;
  // LastKnownGpioState - Last known GPIO state (normally last reported to Foundation.Core)
  uint8_t LastKnownGpioState;    // 1 = high, 0 = low, 0xff = unknown
  // GlitchBitMask - Bit masl with the number of consecutive 1s or 0s before success.
  uint32_t GlitchBitMask;
  // GlitchCollectedStates - The states, shifted from right to left on each timeout, initialized to 0 by isr
  uint32_t GlitchCollectedStates;
  // GlitchCollectionCount - The number of states already processed for this GPIO. Set to 0 by isr
  uint16_t GlitchCollectionCount;
  // NeededMinGlitchTimeouts - The minimum nuber of state bits that must all agree for glitch
  uint16_t NeededMinGlitchTimeouts;
  // GlitchTimeoutsToSkip - Establishes the timeouts to skip if > 32 bits would be needed
  uint8_t GlitchTimeoutsToSkip;
  // The number of debounce timer experations needed to satisfy user's config
  int16_t NeededDebounceTimeouts;
  // This counter counts down to zero, this ends the debounce period allowing
  // new GPIO interrupts to be received
  uint16_t DebounceDownCounter;
};

// Each row represents one GPIO that could be active on meadow
static struct interruptPinMap_s gpioDebounceData[] =
{
// PinId              IDRAddress                    Current State    LKS  GBM GCS GCC NGT GTS NDT DDC
  {0x04, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 00 A0
  {0x05, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 01 A1
  {0x06, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 02 A2
  {0x07, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 03 A3
  {0x20, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 04 A4
  {0x21, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 05 A5
  {0x2A, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 06 SCK
  {0x15, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 07 MOSI
  {0x2B, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 08 MOSO
  {0x89, STM32_GPIOI_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 09 D00
  {0x7D, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 10 D01
  {0x26, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 11 D02
  {0x18, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 12 D03
  {0x19, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 13 D04
  {0x43, STM32_GPIOE_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 14 D05
  {0x63, STM32_GPIOG_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 15 D06
  {0x1F, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 16 D07
  {0x1E, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 17 D08
  {0x29, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 18 D09
  {0x7A, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 19 D10
  {0x11, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 20 D11
  {0x16, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 21 D12
  {0x17, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 22 D13
  {0x10, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0},   // 23 D14
  {0x27, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, updipstate_uncfg, 0xff, 0,  0,  0,  0,  0,  0,  0}    // 24 D15
};

#define MEADOW_UPD_NUMB_GPIO_INTERRUPTS (25)
#define MEADOW_PERIODIC_FILTER_TIMER_NUMBER (10)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int upd_gpio_interrupt(int irq, void *context, void *arg);
static int upd_config_interrupt_prep_timer(int stm32_timer_numb);
static int upd_gpio_interrupt_send(struct interruptPinMap_s *gpioMapTblPtr, uint8_t state);
static int upd_process_gpio_debounce(struct interruptPinMap_s *gpioMapTblPtr);
static int upd_process_gpio_glitch(struct interruptPinMap_s *gpioMapTblPtr);
//static int process_glitch_timeout(struct interruptPinMap_s *gpioMapTblPtr);

static int upd_periodic_timeout_isr(int irq, void *context, void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool _isInitializationNeeded = true;
static struct stm32_tim_dev_s *_periodicTimer;

// These are optimizations so we don't generate unusable interrupts
static volatile int numbGpioConfigured = 0;  // incremented and decrementd by config 
static volatile int numbGpioMonitored = 0;      // increment by ISR decremented by periodic debouncer

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// There has been a transition on a GPIO pin that we've been ask to monitor
static int upd_gpio_interrupt(int irq, void *context, void *arg)
{  
  // Is there any work to do?
  if(numbGpioConfigured < 1)
    return OK;

  struct interruptPinMap_s *gpioMapTblPtr = (struct interruptPinMap_s *)arg;

  // Properly setup?
  if(gpioMapTblPtr == NULL)
  {
    syslog(1, "upd-(GPIO isr)-- PinId:0x%02x <not setup> gpioMapTblPtr == NULL, exit!\n", gpioMapTblPtr->PinId);
    return OK;
  }

  // Unless waiting for gpio interrupt, ignore
  if(gpioMapTblPtr->CurrentProcessState != updipstate_wait_gpio_isr)
  {
    // After first time switch bounce will exit here
    syslog(1, "upd-(GPIO isr)--<running, bounce?> PinId:0x%02x\n", gpioMapTblPtr->PinId);
    return OK;
  }

  // Check configuration to know what to do first, glitch or debounce
  if(gpioMapTblPtr->NeededMinGlitchTimeouts > 0)
  {
  syslog(1, "upd-(GPIO isr)--PinId:0x%02x, Proc glitch\n", gpioMapTblPtr->PinId);
    // Glitch starts before Debounce
    gpioMapTblPtr->GlitchCollectionCount = 0;
    gpioMapTblPtr->GlitchCollectedStates = 0;
    gpioMapTblPtr->CurrentProcessState = updipstate_mon_glitch;

    numbGpioMonitored++;
    STM32_TIM_SETMODE(_periodicTimer, STM32_TIM_MODE_UP);
  }
  else if(gpioMapTblPtr->NeededDebounceTimeouts > 0)
  {
  syslog(1, "upd-(GPIO isr)--PinId:0x%02x, Proc debounce debounce\n", gpioMapTblPtr->PinId);
    gpioMapTblPtr->DebounceDownCounter = gpioMapTblPtr->NeededDebounceTimeouts;
    gpioMapTblPtr->CurrentProcessState = updipstate_mon_debounce;

    numbGpioMonitored++;
    STM32_TIM_SETMODE(_periodicTimer, STM32_TIM_MODE_UP);
  }

  return OK;
}

//===============================================================
// DEVELOPER NOTE:
// Debounce recognizes the first state transition and then ignores anything after
//  that for a period of time.
// Glitch filtering ignores the first state transition and waits a period of time
//  and then looks at state to make sure the result is stable
int upd_periodic_timeout_isr(int irq, void *context, void *arg)
{
  int result;
  struct interruptPinMap_s *gpioMapTblPtr;
  static uint32_t periodicTimeoutCount = 0;

  // Acknowledge timer interrupt ASAP to keep timing accurate
  STM32_TIM_ACKINT(_periodicTimer, GTIM_SR_UIF);

  periodicTimeoutCount++;

  // p-m DEBUG Just toggle for fun
  stm32_gpiowrite(HCOM_ONBOARD_D06_PIN_OUTPUT, true);
  static bool dbgState5 = false;
  dbgState5 = !dbgState5;
    stm32_gpiowrite(HCOM_ONBOARD_D05_PIN_OUTPUT, dbgState5);
  // p-m END DEBUGGING STUFF

  // Only needed if at least one gpio configured and active
  if(numbGpioConfigured == 0 || numbGpioMonitored == 0)
  {
  syslog(1, "upd-(GPIO isr)--Exiting cfg & mon both 0\n");
    // p-m NEED TO STOP INTERRUPT HERE?
    return OK;
  }

  // Find all active GPIOs
  // p-m FIND A BETTER WAY TO DO THIS SO ONLY TEST ACTIVE GPIOS?
  for(int offset = 0; offset < MEADOW_UPD_NUMB_GPIO_INTERRUPTS; offset++)
  {
    gpioMapTblPtr = &gpioDebounceData[offset];

    // Monitoring Debounce or Glitch?
    if(gpioMapTblPtr->CurrentProcessState != updipstate_mon_glitch &&
        gpioMapTblPtr->CurrentProcessState != updipstate_mon_debounce)
      continue;   // No, this GPIO not active

    // Monitoring debounce?
    if(gpioMapTblPtr->CurrentProcessState == updipstate_mon_debounce)
    {
      //----- Debounce Filtering -----
      result = upd_process_gpio_debounce(gpioMapTblPtr);
      if(result == gadrv_break)
      {
        break;
      }
      else if(result < 0)
      {
        int errcode = get_errno();
        syslog(LOG_ERR, "%s@%d-mq_open failed: %d\n", __FILE__, __LINE__, errcode);
        return -errcode;
      }
      else
      {
        DEBUGASSERT(result == gadrv_continue);
        continue;
      }
    }

    //----- Glitch Filtering -----
    if(gpioMapTblPtr->GlitchCollectionCount == 0)
      syslog(1, "upd-(debounce)-0x%02x GLITCH Starting\n", gpioMapTblPtr->PinId);

    // For longer delays or faster clocks, we need to skip some timeouts
    // after all we only have 32 bits in which to collect state info
    if(gpioMapTblPtr->GlitchTimeoutsToSkip > 1)
    {
      if(periodicTimeoutCount % gpioMapTblPtr->GlitchTimeoutsToSkip != 0)
      {
        // syslog(1, "udp-(timeout)-0x%02x Skipping timeout:%d\n", gpioMapTblPtr->PinId, periodicTimeoutCount);
        continue;
      }
    }

    // Note: upd_process_gpio_glitch may or may not send interrupt to
    // Foundation.Core
    result = upd_process_gpio_glitch(gpioMapTblPtr);
    //syslog(1, "udp-(gl to)-upd_process_gpio_glitch returned:%d\n", result);
    if(result == gadrv_keepwaiting)
      continue;

    // We reached the end of the glitch filtering
    
    // Switch to debounce? f debounce is also configured and this was not a glitch
    if(gpioMapTblPtr->NeededDebounceTimeouts > 0 && result == gadrv_validtransitiondetected)
    {
      //Debounce is also configured and this was not a glitch
      // Switch to debounce checking
      gpioMapTblPtr->DebounceDownCounter = gpioMapTblPtr->NeededDebounceTimeouts;
      gpioMapTblPtr->CurrentProcessState = updipstate_mon_debounce;
    }
    else
    {
      // Terminate capture for this GPIO
      numbGpioMonitored--;
      gpioMapTblPtr->CurrentProcessState = updipstate_wait_gpio_isr;
      if(numbGpioMonitored == 0)
        break;    // Cannot be more work to do, so quit loop
    }
  }

  // Stop Timer if nothing else to do
  if(numbGpioMonitored == 0)
  {
    int ret = STM32_TIM_SETMODE(_periodicTimer, STM32_TIM_MODE_DISABLED);
    if(ret < 0)
    {
      syslog(1, "udp-(timeout)--STM32_TIM_SETMODE failed:%d\n", ret);
      return ret;
    }
  }

  // p-m DEBUGGING
  stm32_gpiowrite(HCOM_ONBOARD_D06_PIN_OUTPUT, false);
  return OK;
}

//========================================================================
//
int upd_process_gpio_debounce(struct interruptPinMap_s *gpioMapTblPtr)
{
  // This is the start of Debounce processing. Step one is to send an
  // interrupt, if not already sent by glitch filtering.
  if(gpioMapTblPtr->DebounceDownCounter == gpioMapTblPtr->NeededDebounceTimeouts)
  {
    if(gpioMapTblPtr->NeededMinGlitchTimeouts == 0)
    {
      uint8_t newState;
  syslog(1, "upd-(debounce)-0x%02x DEBOUNCE Starting No Glitch\n", gpioMapTblPtr->PinId);

      // Glitch filtering not configured so must send interrupt.
      switch(gpioMapTblPtr->GpioInterruptMode)
      {
        // The following is problematic. We can't read the current GPIO state, because may
        // have changed since generating the interrupt. If the desired interrupt mode is
        // 'rising' or 'falling' we can use this to determine what to send. In the case of
        // 'both' we must assume that the last known state is valid and  send the opposite.
        case gldbintmode_both:
          // Assume the opposite state
          newState = gpioMapTblPtr->LastKnownGpioState == 1 ? 0 : 1;
          syslog(1, "upd-(debounce)-0x%02x gldbintmode_both - sending 0x%02x\n", gpioMapTblPtr->PinId, newState);
          upd_gpio_interrupt_send(gpioMapTblPtr, newState);
          gpioMapTblPtr->LastKnownGpioState = newState;
          break;

        case gldbintmode_falling:
          // Must be high to low transition
          syslog(1, "upd-(debounce)-0x%02x gldbintmode_falling - sending 0x%02x\n", gpioMapTblPtr->PinId, 0);
          upd_gpio_interrupt_send(gpioMapTblPtr, 0);
          gpioMapTblPtr->LastKnownGpioState = 0;
          break;

        case gldbintmode_rising:
          // Must be low to high transition
          syslog(1, "upd-(debounce)-0x%02x gldbintmode_rising - sending 0x%02x\n", gpioMapTblPtr->PinId, 1);
          upd_gpio_interrupt_send(gpioMapTblPtr, 1);
          gpioMapTblPtr->LastKnownGpioState = 1;
          break;

        case gldbintmode_none:
        default:
          syslog(1, "upd-(init db)-0x%02x gldbintmode_none do nothing\n", gpioMapTblPtr->PinId);
          break;
        break;
      }
    }
    else
      syslog(1, "upd-(debounce)-0x%02x DEBOUNCE Starting following Glitch\n", gpioMapTblPtr->PinId);
  }

  // Decrement the timeout count. When it reaches 0 we can
  // re-enbled the gpio interrupts
  gpioMapTblPtr->DebounceDownCounter--;
  if(gpioMapTblPtr->DebounceDownCounter > 0)
    return gadrv_continue;   // Not done yet

  // Finished with debounce timing. When debounce completes there's
  // nothing else to follow.
  syslog(1, "upd-(debounce)-0x%02x DEBOUNCE EXITING\n", gpioMapTblPtr->PinId);
  numbGpioMonitored--;

  // Terminate this GPIO's capture.  So, allow GPIO interrupts to be processed
  gpioMapTblPtr->CurrentProcessState = updipstate_wait_gpio_isr;

  if(numbGpioMonitored == 0)
    return gadrv_break;    // Cannot be more work to do, so quit loop too

  return gadrv_continue;   // Go to next gpio
}

//========================================================================
// Glitch filtering is a bit more complex and it's done here.
int upd_process_gpio_glitch(struct interruptPinMap_s *gpioMapTblPtr)
{  
  // // DEBUGGING - SAVE PREVIOUS STATE
  // uint32_t oldHistoryBits = gpioMapTblPtr->GlitchCollectedStates;
  // // DEBUGGING - SAVE PREVIOUS STATE

  // Set 1 bit for current state
  if(gpioMapTblPtr->GlitchCollectionCount > 0)
    gpioMapTblPtr->GlitchCollectedStates <<= 1;   // Don't shift if first time

  // Add the proper state bit
  uint8_t pinNumb = gpioMapTblPtr->PinId & 0x0f;
  uint32_t idrRegisterValues = *((uint32_t *)(gpioMapTblPtr->IDRAddress));
  uint8_t currentState = (idrRegisterValues & (1 << pinNumb)) > 0 ? 1 : 0;

  if(currentState == 1)
    gpioMapTblPtr->GlitchCollectedStates |= 1;
  
  gpioMapTblPtr->GlitchCollectionCount++;

  // Save the last x bits of the state
  uint32_t lastXBits =  gpioMapTblPtr->GlitchCollectedStates & gpioMapTblPtr->GlitchBitMask;
  
  // // DEBUGGING - SAVE PREVIOUS STATE
  // syslog(1, "upd_(glitch)-%02d--0x%02x, state:%s, prev-0x%08x, new-0x%08x, new & mask:0x%08x\n",
  //         gpioMapTblPtr->GlitchCollectionCount,
  //         gpioMapTblPtr->PinId,
  //         currentState == 1 ? "TRUE" : "false",
  //         oldHistoryBits,
  //         gpioMapTblPtr->GlitchCollectedStates,
  //         lastXBits);
  // // DEBUGGING - SAVE PREVIOUS STATE

  // Have we gotten enough info to determine stable state?
  if(gpioMapTblPtr->GlitchCollectionCount < gpioMapTblPtr->NeededMinGlitchTimeouts)
    return gadrv_keepwaiting;   // Not yet

syslog(1, "upd_(glitch)-0x%02x GlitchCollectionCount:%d < NeededMinGlitchTimeouts:%d STOP\n",
  gpioMapTblPtr->PinId, gpioMapTblPtr->GlitchCollectionCount,
   gpioMapTblPtr->NeededMinGlitchTimeouts);

  // We've checked enough
  // Now if these bits are all 0 or equal to mask we have a stable input state
  if(lastXBits == 0 || lastXBits == gpioMapTblPtr->GlitchBitMask)
  {
    // It's been stable long enough
    syslog(1, "upd_(glitch)-0x%02x Enough waiting and STABLIZE. lastXBits:0x%08x, state: old:%d, new:%d\n",
              gpioMapTblPtr->PinId, lastXBits, gpioMapTblPtr->LastKnownGpioState, currentState);

    bool isRising = gpioMapTblPtr->LastKnownGpioState < currentState;
    switch(gpioMapTblPtr->GpioInterruptMode)
    {
      case gldbintmode_both:
        syslog(1, "upd_(glitch)-0x%02x Send YES, gldbintmode_both\n", gpioMapTblPtr->PinId);
        upd_gpio_interrupt_send(gpioMapTblPtr, currentState);
        break;

      case gldbintmode_falling:
        if(!isRising)
        {
          syslog(1, "upd_(glitch)-0x%02x Send YES, Falling and config gldbintmode_falling\n", gpioMapTblPtr->PinId);
          upd_gpio_interrupt_send(gpioMapTblPtr, currentState);
        }
        else
        {
          syslog(1, "upd_(glitch)-0x%02x Send NO, Rising but config gldbintmode_falling\n", gpioMapTblPtr->PinId);
        }
        break;

      case gldbintmode_rising:
        if(isRising)
        {
          syslog(1, "upd_(glitch)-0x%02x Send YES, Rising and config gldbintmode_rising\n", gpioMapTblPtr->PinId);
          upd_gpio_interrupt_send(gpioMapTblPtr, currentState);
        }
        else
        {
          syslog(1, "upd_(glitch)-0x%02x Send NO, Rising but config gldbintmode_rising\n", gpioMapTblPtr->PinId);
        }
        break;

      case gldbintmode_none:
      default:
        syslog(1, "upd_(glitch)-0x%02x Send NO, config gldbintmode_none\n", gpioMapTblPtr->PinId);
        break;
      break;
    }

    gpioMapTblPtr->LastKnownGpioState = currentState;
    return gadrv_validtransitiondetected;
  }
  else
  {
    syslog(1, "upd_(glitch)-%02d--0x%02x Enough time, NOT good enough 0x%08x\n",
            gpioMapTblPtr->GlitchCollectionCount, gpioMapTblPtr->PinId,
            lastXBits);
  }
  return gadrv_keepwaiting;
}

//===============================================================
// This will forward an interrupt to Foundation.Core
int upd_gpio_interrupt_send(struct interruptPinMap_s *gpioMapTblPtr, uint8_t state)
{
  int ret;
  extern mqd_t s_int_queue;

  // Forward to Foundation
  char queue_buffer[QUEUE_MSG_SIZE];
  queue_buffer[0] = gpioMapTblPtr->PinId;
  queue_buffer[1] = state;

  ret = mq_send(s_int_queue, queue_buffer, QUEUE_MSG_SIZE, 0);
  if(ret < 0)
  {
    syslog(1, "udp-(send)--mq_send failed:%d, errno:%d\n", ret, get_errno());
    return ret;
  }

  return ret;
}

//========================================================
// Timer settup is here. This should only be called once
// to prepare both timers for operation.
static int upd_config_interrupt_prep_timer(int stm32_timer_numb)
{
  int ret;
  struct stm32_tim_dev_s *tempTimer;

  // for 100 microsec
  uint32_t frequency = STM32_APB2_TIM10_CLKIN / 100; // 1,920,000 MHz;
  uint32_t period = 192 - 1;
  xcpt_t isrHandler = upd_periodic_timeout_isr;

  // -- for 1 microsec --
  // frequency = STM32_APB2_TIM10_CLKIN;
  // period = 192 - 1;
  // -- for 1 millisec --
  // frequency = STM32_APB2_TIM10_CLKIN / 100; // 1,920,000 MHz
  // period = 1920 - 1;                        // 1 millisec
  
  tempTimer = stm32_tim_init(stm32_timer_numb);
  if(tempTimer == NULL)
  {
    syslog(1, "udp-(timer cfg)--initializing failed, returned NULL\n");
    return OK;
  }
  
  // Increasing frequency increases the frequency
  syslog(1, "udp-(timer cfg)--initialization debounceFreq:%d, STM32_APB2_TIM10_CLKIN:%d\n",
          frequency, STM32_APB2_TIM10_CLKIN);

  // This determines the prescaler value 0 - 65535
  int prescaler = STM32_TIM_SETCLOCK(tempTimer, frequency);

  // Increasing period decreases the frequency
  STM32_TIM_SETPERIOD(tempTimer, period);

  // arg (third parameter) is a pointer that's returned in the isr handler
  ret = STM32_TIM_SETISR(tempTimer, isrHandler, NULL, 0);
  if(ret < 0)
  {
    syslog(1, "udp-(timer cfg)--STM32_TIM_SETISR failed:%d\n", ret);
    return ret;
  }
  syslog(1, "udp-(timer cfg)--initialization debounceFreq:%d => prescaler:%d, Period:%d\n",
          frequency, prescaler, period);

  // Prevent interrupts until needed
  STM32_TIM_SETMODE(tempTimer, STM32_TIM_MODE_DISABLED);

  // Finish set up
  STM32_TIM_ACKINT(tempTimer, GTIM_SR_UIF);
  STM32_TIM_ENABLEINT(tempTimer, GTIM_DIER_UIE);

  _periodicTimer = tempTimer;

  syslog(1, "udp-(timer cfg)--SUCCESS EXIT\n\n");
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Called from meadow-upd.c to configure a gpio for monitoring
int upd_config_interrupt(struct upd_gpio_int_config* cfg)
{
  struct interruptPinMap_s *gpioMapTblPtr;
  uint32_t designator = cfg->port << 4 | cfg->pin;
  uint32_t pinMapOffset;
  int gpio_count;
  int ret;

  // Walk the gpio data array to find the desired entry
  for(gpio_count = 0; gpio_count < MEADOW_UPD_NUMB_GPIO_INTERRUPTS; gpio_count++)
  {
    if(gpioDebounceData[gpio_count].PinId == designator)
    {
      pinMapOffset = gpio_count;
      break;
    }
  }

  if(gpio_count == MEADOW_UPD_NUMB_GPIO_INTERRUPTS)
  {
    // GPIO not found in data table?
    syslog(1, "upd-(cfg)---BAD NEWS! NO TABLE ENTRY FOR %d in gpioDebounceData\n", designator);
    return -1;
  }

  // Save the correct entry's address
  gpioMapTblPtr = &gpioDebounceData[pinMapOffset];
  
  // Only do this stuff once
  if(_isInitializationNeeded)
  {
    ret = upd_config_interrupt_prep_timer(MEADOW_PERIODIC_FILTER_TIMER_NUMBER);
    if(ret < 0)
    {
      syslog(1, "upd-(cfg)---BAD NEWS! upd_config_interrupt_prep_timer failed\n");
      return -1;
    }
    _isInitializationNeeded = false;

    // p-m DEBUG STUFF
    stm32_configgpio(HCOM_ONBOARD_D05_PIN_OUTPUT);
    stm32_configgpio(HCOM_ONBOARD_D06_PIN_OUTPUT);
    stm32_configgpio(HCOM_ONBOARD_D07_PIN_OUTPUT);
    stm32_configgpio(HCOM_ONBOARD_D08_PIN_OUTPUT);
  }

  if(cfg->enable)
  {
    // Get the current state which can be used to detect changes when the first
    // the first interrupt arrives.
    uint32_t idrRegisterValues = *((uint32_t *)(gpioMapTblPtr->IDRAddress));
    uint8_t pinNumb = gpioMapTblPtr->PinId & 0x0f;

    // Not sure why but 1 = low
    gpioMapTblPtr->LastKnownGpioState = (idrRegisterValues & (1 << pinNumb)) > 0 ? 1 : 0;
syslog(1, "in config: setting LastKnownGpioState:%d\n", gpioMapTblPtr->LastKnownGpioState);

    // Glitch config
    // Note: the available configuration is 0, 0.1 - 1000 millisec, Foundation.Core
    // will provide us with a value of 1 - 10000. So as long as the timer's timeout
    // value is 100 usec everything should work.
    uint16_t glDuration = (uint16_t)cfg->glitchDuration_us2;
    gpioMapTblPtr->GlitchTimeoutsToSkip = (glDuration / 32) + 1;
    gpioMapTblPtr->NeededMinGlitchTimeouts = glDuration / gpioMapTblPtr->GlitchTimeoutsToSkip;

    // Create a mask for the number of timeouts
    gpioMapTblPtr->GlitchBitMask = 0;
    for(int i = 0; i < gpioMapTblPtr->NeededMinGlitchTimeouts; i++)
    {
      gpioMapTblPtr->GlitchBitMask <<= 1;
      gpioMapTblPtr->GlitchBitMask |= 1;
    }

    syslog(1, "upd-(cfg)-0x%02x (P%c%d)--GlDuration:%d, GlitchBitMask:0x%08x, GlitchTimeoutsToSkip:%d, glMinTimeouts:%d\n",
              gpioMapTblPtr->PinId,
              ((gpioMapTblPtr->PinId) >> 4) + 'A', gpioMapTblPtr->PinId & 0x0f,
              cfg->glitchDuration_us2,
              gpioMapTblPtr->GlitchBitMask,
              gpioMapTblPtr->GlitchTimeoutsToSkip,
              gpioMapTblPtr->NeededMinGlitchTimeouts);

    // Debounce config
    // Not much needed here as we pass the interrupt asap and then just wait
    gpioMapTblPtr->NeededDebounceTimeouts = cfg->debounceDuration_us2;

    // none = 0, rising = 1, falling = 2 & both = 3 (must match F7GPIOManager_interrupts.cs
    // in WireInterrupt()
    gpioMapTblPtr->GpioInterruptMode = (cfg->risingEdge & 0x01) | (cfg->fallingEdge & 0x01) << 1;

    // Tell Nuttx interrupts parameters
    ret = stm32_gpiosetevent(
    gpioMapTblPtr->PinId,
    cfg->risingEdge,      // risingEdge,
    cfg->fallingEdge,     // fallingEdge,
    0,                    // event
    upd_gpio_interrupt,   // function to call
    gpioMapTblPtr);       // table entry pointer
    
    gpioMapTblPtr->CurrentProcessState = updipstate_wait_gpio_isr;
    numbGpioConfigured++;
  }
  else
  {
    // Disable
    numbGpioConfigured--;    
    gpioMapTblPtr->CurrentProcessState = updipstate_uncfg;

    // Tell Nuttx to forget about interrupts
    ret = stm32_gpiosetevent(
        gpioMapTblPtr->PinId,
        false, false, 0, NULL, NULL);
  }
  return ret;
}
