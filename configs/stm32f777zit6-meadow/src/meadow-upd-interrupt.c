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

#include <nuttx/timers/timer.h>
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

enum debounceStateEnum_e
{
  dbse_unconfig,    // not configured
  dbse_config,      // Configured, waiting on first interrupt
  dbse_wait_isr,    // waiting for an interrupt
  dbse_timed,       // gpio is being monitored for debounce
};

enum InterruptMode_e
{
  dbime_none,
  dbime_rising,
  dbime_falling,
  dbime_both
};

// The following struct stores the informtion needed for debounce
struct interruptPinMap_s
{
  // Represents the CPU Pin identifier (e.g. PD9 but D9 = 39)
  const uint8_t PinId;
  // Address where the Input Data Register is
  const uint32_t IDRAddress;
  // DebounceMask the number of consecutive 1s or 0s before debounced.
  uint32_t DebounceMask;
  // DebounceDivisor - Time scale beyond 32 bits in mask
  uint32_t DebounceDivisor;
  // HistoryBits are shifted from right to left on each tick, set to 0 by isr
  uint32_t HistoryBits;
  // The number of state bits processed for this point. Set to 0 by isr
  uint8_t HistoryCount;
  // PrevReportedState - Last reported to Foundation
  uint8_t PrevReportedState;    // 1 = true, 0 = false, 0xff = unknown
  // DebounceStateEnum - tracks the current state of processing gpio
  uint8_t DebounceStateEnum;
  // The number of state bits needed for debounce (compared with HistoryCount)
  uint8_t NeededDebounceBits;
  // // The number of state bits needed for glitch (compared with HistoryCount)
  // uint8_t NeededGlitchBits;
  // Changes interested in:None = 0, Rising = 1, Falling = 2 and Both = 3;
  uint8_t InterruptMode;
};

static struct interruptPinMap_s gpioDebounceData[] =
{
// PinId              IDRAddress                  DM DD HB HC  PRS   Dbounce state ND IM
  {0x04, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 00 A0
  {0x05, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 01 A1
  {0x06, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 02 A2
  {0x07, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 03 A3
  {0x20, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 04 A4
  {0x21, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 05 A5
  {0x2A, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 06 SCK
  {0x15, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 07 MOSI
  {0x2B, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 08 MOSO
  {0x89, STM32_GPIOI_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 09 D00
  {0x7D, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 10 D01
  {0x26, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 11 D02
  {0x18, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 12 D03
  {0x19, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 13 D04
  {0x43, STM32_GPIOE_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 14 D05
  {0x63, STM32_GPIOG_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 15 D06
  {0x1F, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 16 D07
  {0x1E, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 17 D08
  {0x29, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 18 D09
  {0x7A, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 19 D10
  {0x11, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 20 D11
  {0x16, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 21 D12
  {0x17, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 22 D13
  {0x10, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0},   // 23 D14
  {0x27, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0, 0xff, dbse_unconfig, 0, 0}    // 24 D15
};

#define MEADOW_UPD_NUMB_GPIO_INTERRUPTS (25)
#define MEADOW_DEBOUNCE_TIMER_NUMBER (10)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int upd_gpio_interrupt(int irq, void *context, void *arg);
static int debounce_timer_handler(int irq, void *context, void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/
static bool isTimerInitialized;
static struct stm32_tim_dev_s *_debounceTimer;

// These are optimizations so we don't waste time
static volatile int numbDebounceConfigured = 0;  // incremented and decrementd by config 
static volatile int numbDebounceActive = 0;      // increment by ISR decremented by periodic debouncer

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int upd_gpio_interrupt_timer_config(void)
{
  int ret;

  if(isTimerInitialized)
    return OK;

  stm32_configgpio(HCOM_ONBOARD_D05_PIN_OUTPUT);
  stm32_configgpio(HCOM_ONBOARD_D06_PIN_OUTPUT);
  stm32_configgpio(HCOM_ONBOARD_D07_PIN_OUTPUT);
  stm32_configgpio(HCOM_ONBOARD_D08_PIN_OUTPUT);

  syslog(1, "udp-(timer cfg)--Begin initializing timer #%d for debounce\n", MEADOW_DEBOUNCE_TIMER_NUMBER);

  // TIM7 global interrupt Position:55 Priority:62, NVIC Offset:0x0000011C
  _debounceTimer = stm32_tim_init(MEADOW_DEBOUNCE_TIMER_NUMBER);
  if(_debounceTimer == NULL)
  {
    syslog(1, "udp-(timer cfg)--initializing failed, returned NULL\n");
    return OK;
  }
  
  // Increasing frequency increases the frequency
  // STM32_APB2_TIM10_CLKIN = 192,000,000
  // uint32_t debounceFreq = STM32_APB2_TIM10_CLKIN;  // for 1 microsec
  uint32_t debounceFreq = STM32_APB2_TIM10_CLKIN / 100;   // for 1 millisec
  syslog(1, "udp-(timer cfg)--initialization debounceFreq:%d, STM32_APB2_TIM10_CLKIN:%d\n",
          debounceFreq, STM32_APB2_TIM10_CLKIN);

  // This determines the prescaler value 0 - 65535
  int prescaler = STM32_TIM_SETCLOCK(_debounceTimer, debounceFreq); 
  syslog(1, "udp-(timer cfg)--initialization debounceFreq:%d => prescaler:%d\n",
          debounceFreq, prescaler);

  // arg (third parameter) is a pointer that's returned in the isr handler
  ret = STM32_TIM_SETISR(_debounceTimer, (xcpt_t)debounce_timer_handler, NULL, 0);
  if(ret < 0)
  {
    syslog(1, "udp-(timer cfg)--STM32_TIM_SETISR failed:%d\n", ret);
    return ret;
  }

  uint32_t debouncePeriod = 1920 - 1;   // for 1 millisec
  // uint32_t debouncePeriod = 192; // for 1 microsec
  STM32_TIM_SETPERIOD(_debounceTimer, debouncePeriod);

  syslog(1, "udp-(timer cfg)--initialization debounceFreq:%d => prescaler:%d, debouncePeriod:%d\n",
          debounceFreq, prescaler, debouncePeriod);

  STM32_TIM_ACKINT(_debounceTimer, GTIM_SR_UIF);
  STM32_TIM_ENABLEINT(_debounceTimer, GTIM_DIER_UIE);

  syslog(1, "udp-(timer cfg)--SUCCESS EXIT\n\n");
  isTimerInitialized = true;
  return OK;
}

//===============================================================
static int upd_gpio_interrupt_forward(uint8_t pinId, uint8_t state)
{
  // Forwared to Foundation
  queue_buffer[0] = pinId;
  queue_buffer[1] = state;
  return mq_send(s_int_queue, queue_buffer, QUEUE_MSG_SIZE, 0);
}

//===============================================================
// DEVELOPER NOTE:
// Debounce recognizes the first state transition and then ignores anything after that
//  for a period of time.
//
int debounce_timer_handler(int irq, void *context, void *arg)
{
  int ret;

  // p-m DEBUG Just toggle for fun
  static bool dbgState5 = false;
  dbgState5 = !dbgState5;
    stm32_gpiowrite(HCOM_ONBOARD_D05_PIN_OUTPUT, dbgState5);
  // DEBUG Just toggle for fun

  // p-m more DEBUGGING STUFF
  static uint32_t tickHookCounter = 0;
  static clock_t prevHookCount = 0;
  if(tickHookCounter == 0)
    prevHookCount = clock_systimer();
  tickHookCounter++;
  if(tickHookCounter % (30 * (1000000/CONFIG_USEC_PER_TICK)) == 0)
  {
    clock_t now = clock_systimer();
    syslog(1, "udp-(tick)-30 seconds have passed (%d from last)\n", now - prevHookCount);
    prevHookCount = now;
  }
  // DEBUGGING STUFF

  STM32_TIM_ACKINT(_debounceTimer, GTIM_SR_UIF);

  // Only if at least one gpio configured and active
  if(numbDebounceActive == 0 || numbDebounceConfigured == 0)
  {
    return OK;
  }

  struct interruptPinMap_s *pinDbncAddr;
  for(int offset = 0; offset < MEADOW_UPD_NUMB_GPIO_INTERRUPTS; offset++)
  {
    pinDbncAddr = &gpioDebounceData[offset];

    // Only interested in GPIOs to be monitored
    if(pinDbncAddr->DebounceStateEnum != dbse_timed)
      continue;
    
    // For longer delays or faster clocks, we must skip some ticks
    // after all we've only got 32 HistoryBits.
    if(pinDbncAddr->DebounceDivisor > 1)
    {
      if(tickHookCounter % pinDbncAddr->DebounceDivisor != 0)
      {
        syslog(1, "udp-(tick)-0x%02x Skipping tick:%d\n", pinDbncAddr->PinId, tickHookCounter);
        continue;
      }
    }

    // DEBUGGING - SAVE PREVIOUS STATE
    uint32_t oldHistoryBits = pinDbncAddr->HistoryBits;
    // DEBUGGING - SAVE PREVIOUS STATE

    // Read all 16 pins for this port
    uint8_t pinNumb = pinDbncAddr->PinId & 0x0f;
    uint32_t idrRegisterValues = *((uint32_t *)(pinDbncAddr->IDRAddress));
    uint8_t currentState = (idrRegisterValues & (1 << pinNumb)) > 0 ? 1 : 0;

    // Set 1 bit for current state
    if(pinDbncAddr->HistoryCount > 0)
      pinDbncAddr->HistoryBits <<= 1;   // Don't shift if first time
    if(currentState == 1)
      pinDbncAddr->HistoryBits |= 1;

    pinDbncAddr->HistoryCount++;

    // // --- Glitch Filtering ---
    // // Note this will never be true if NeededGlitchBits == 0 and it
    // // Note: A glitch count of 1 will uses the time between isr and
    // // this code executes which is unknown.
    // if(pinDbncAddr->NeededGlitchBits == pinDbncAddr->HistoryCount)
    // {
    //   // Glitch filtering ignores the first state transition and waits a period of time
    //   //  and then looks at state to make sure the result is stable
    //   if(currentState == pinDbncAddr->PrevReportedState)
    //   {
    //     syslog(1, "udp-(tick)-%02d--0x%02x, GLITCH! state now:%s, oldHIst:0x%08x, hist:0x%08x, Needed Glitch Bits:%02d\n",
    //             pinDbncAddr->HistoryCount,
    //             pinDbncAddr->PinId,
    //             currentState == 1 ? "TRUE" : "false",
    //             oldHistoryBits,
    //             pinDbncAddr->HistoryBits,
    //             pinDbncAddr->NeededGlitchBits);

    //     // Didn't find any change for the delay, must be a glitch
    //     pinDbncAddr->DebounceStateEnum = dbse_wait_isr;    // Terminate detection
    //     numbDebounceActive--;  // So this function knows when there's no work to do.
    //     continue;
    //   }
    // }

    // Save the last x bits of the state
    uint32_t lastXBits =  pinDbncAddr->HistoryBits & pinDbncAddr->DebounceMask;
    syslog(1, "udp-(tick)-%02d--0x%02x, state:%s, prev-0x%08x, new-0x%08x, new & mask:0x%08x\n",
            pinDbncAddr->HistoryCount,
            pinDbncAddr->PinId,
            currentState == 1 ? "TRUE" : "false",
            oldHistoryBits,
            pinDbncAddr->HistoryBits,
            lastXBits);

    // Have we gotten enough info to determine stable state?
    if(pinDbncAddr->HistoryCount < pinDbncAddr->NeededDebounceBits)
      continue;   // Not yet

    // If these bits are all 0 or equal to mask we have a stable input state
    if(lastXBits == 0 || lastXBits == pinDbncAddr->DebounceMask)
    {
      // It's been stable long enough
      syslog(1, "udp-(tick)-0x%02x Enough waiting and STABLIZE. lastXBits:0x%08x, state: old:%d, new:%d\n",
                pinDbncAddr->PinId, lastXBits, pinDbncAddr->PrevReportedState, currentState);

      // Ignore if no state change has occurred since the last reported to Foundation.Core, ignore.
      if(pinDbncAddr->PrevReportedState == currentState)
      {
         syslog(1, "udp-(tick)-0x%02x Send NO,  No change\n",
               pinDbncAddr->PinId);
      }
      else
      {
        ret = OK;
        switch(pinDbncAddr->InterruptMode)
        {
          case dbime_both:
            syslog(1, "udp-(tick)-0x%02x Send YES, dbime_both\n", pinDbncAddr->PinId);
            ret = upd_gpio_interrupt_forward(pinDbncAddr->PinId, currentState);
            break;

          case dbime_falling:
            if(lastXBits == 0)    // Means all significant bits 0
            {
              syslog(1, "udp-(tick)-0x%02x Send YES,  Falling and config dbime_falling\n", pinDbncAddr->PinId);
              ret = upd_gpio_interrupt_forward(pinDbncAddr->PinId, currentState);
            }
            else
            {
              syslog(1, "udp-(tick)-0x%02x Send NO,  Rising but config dbime_falling\n", pinDbncAddr->PinId);
            }
            break;

          case dbime_rising:
            if(lastXBits == pinDbncAddr->DebounceMask)    // Equal means all significant bits 1
            {
              syslog(1, "udp-(tick)-0x%02x Send YES,  Rising and config dbime_rising\n", pinDbncAddr->PinId);
              ret = upd_gpio_interrupt_forward(pinDbncAddr->PinId, currentState);
            }
            else
            {
              syslog(1, "udp-(tick)-0x%02x Send NO,  Rising but config dbime_rising\n", pinDbncAddr->PinId);
            }
            break;

          case dbime_none:
          default:
            syslog(1, "udp-(tick)-0x%02x Send NO,  config dbime_none\n", pinDbncAddr->PinId);
            break;
          break;
        }  
        syslog(1, "udp-(tick)-0x%02x sent input point (ret:%d)\n", pinDbncAddr->PinId, ret);
        // We'll save the current to help detect glitches
        pinDbncAddr->PrevReportedState = currentState;
      }
      syslog(1, "udp-(tick)-0x%02x DONE with this input point, Wait for next interrupt\n",
              pinDbncAddr->PinId);

      // Allow ISR to again monitor this GPIO for interrupts
      pinDbncAddr->DebounceStateEnum = dbse_wait_isr;    // Terminate capture
      numbDebounceActive--;      // So this function knows when there's no work to do.
      // Stop Timer if nothing to do
      if(numbDebounceActive == 0)
      {
        ret = STM32_TIM_SETMODE(_debounceTimer, STM32_TIM_MODE_DOWN);
        if(ret < 0)
        {
          syslog(1, "udp-(tick)--STM32_TIM_SETMODE failed:%d\n", ret);
          return ret;
        }
      }
    }
    else
    {
      syslog(1, "udp-(tick)-%02d--0x%02x Enough waiting but NOT good enough.\n",
              pinDbncAddr->HistoryCount, pinDbncAddr->PinId);
    }
  }
    return OK;
}

//===============================================================
// There has been a transition on the GPIO
static int upd_gpio_interrupt(int irq, void *context, void *arg)
{
  // Only true if at least one gpio configured for interrupts
  if(numbDebounceConfigured == 0)
    return OK;

  struct interruptPinMap_s *pinDbncAddr = (struct interruptPinMap_s *)arg;

  // Properly setup?
  if(pinDbncAddr == NULL || pinDbncAddr->DebounceStateEnum == dbse_unconfig)
  {
    syslog(1, "upd-(isr)-- <not setup> pinDbncAddr == NULL [Nothing to do]\n");
    return OK;
  }

  if(pinDbncAddr->DebounceStateEnum == dbse_unconfig)
  {
    syslog(1, "upd-(isr)-- <not configured> PinId:0x%02x, pinDbncAddr:%p, DebounceMask:0x%08x\n",
            pinDbncAddr->PinId, pinDbncAddr, pinDbncAddr->DebounceMask);
    return OK;
  }

  // Id not configured or already actively being timed? If yes, ignore.
  if(pinDbncAddr->DebounceStateEnum == dbse_timed)
  {
    syslog(1, "upd-(isr)-- <already active> PinId:0x%02x, pinDbncAddr:%p, DebounceMask:0x%08x\n",
            pinDbncAddr->PinId, pinDbncAddr, pinDbncAddr->DebounceMask);
    return OK;
  }

  // Prepare to do debounce checking
  pinDbncAddr->HistoryCount = 0;
  pinDbncAddr->HistoryBits = 0;  
  pinDbncAddr->DebounceStateEnum = dbse_timed;
  numbDebounceActive++;

  // Start the timer to drive debouce checking
  STM32_TIM_SETMODE(_debounceTimer, STM32_TIM_MODE_UP);  
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int upd_config_interrupt(struct upd_gpio_int_config* cfg)
{
  struct interruptPinMap_s *pinDbncAddr;
  int ret;
  int i;

  uint32_t designator = cfg->port << 4 | cfg->pin;
  uint32_t pinMapOffset;

  ret = upd_gpio_interrupt_timer_config();
  if(ret < 0)
  {
    syslog(1, "upd-(cfg)---BAD NEWS! upd_gpio_interrupt_timer_config failed\n");
    return -1;
  }

  // Walk the gpio data to find the matching entry
  for(i = 0; i < MEADOW_UPD_NUMB_GPIO_INTERRUPTS; i++)
  {
    if(gpioDebounceData[i].PinId == designator)
    {
      pinMapOffset = i;
      break;
    }
  }

  if(i == MEADOW_UPD_NUMB_GPIO_INTERRUPTS)
  {
    // Coding error in gpio data table?
    syslog(1, "upd-(cfg)---BAD NEWS! NO TABLE ENTRY FOR %d in gpioDebounceData\n", designator);
    return -1;
  }

  // Save the correct entry
  pinDbncAddr = &gpioDebounceData[pinMapOffset];
  if(cfg->enable)
  {
    // Get the current state which can be used to detect changes when the first
    // interrupt arrives.
    uint8_t pinNumb = pinDbncAddr->PinId & 0x0f;
    uint32_t idrRegisterValues = *((uint32_t *)(pinDbncAddr->IDRAddress));
    bool currentState = idrRegisterValues & (1 << pinNumb);
    pinDbncAddr->PrevReportedState = currentState == 0 ? 0 : 1;

    // Cannot control the tick rate so we need to determine if we need to skip ticks
    uint32_t ticksNeeded = ((cfg->debounceDuration * 1000)/CONFIG_USEC_PER_TICK);
    pinDbncAddr->DebounceDivisor = (ticksNeeded / 32) + 1;

    // Create a mask for the number of ticks
    pinDbncAddr->DebounceMask = 0;
    for(i = 0; i < ticksNeeded / pinDbncAddr->DebounceDivisor; i++)
    {
      pinDbncAddr->DebounceMask <<= 1;
      pinDbncAddr->DebounceMask |= 1;
    }

    pinDbncAddr->NeededDebounceBits = ticksNeeded;

    // if(cfg->glitchFilterCycleCount == 0)
    // {
    //   // Zero is okay, it means none. But, 1 is a problem since there's
    //   // nothing to compare it with (i.e. cannot determine if changed)
    //   pinDbncAddr->NeededGlitchBits = 0;
    // }
    // else
    // {
    //   // The one who configured wanted some glitch filtering. Therefore,
    //   // Don't allow 0 or 1, must collect at least 2 gpio states for a
    //   // valid compare. And 2 means 1 full timing period
    //   uint8_t calcGlitchBits = ((cfg->glitchFilterCycleCount * 1000)/CONFIG_USEC_PER_TICK);
    //   if(calcGlitchBits < 2)
    //     pinDbncAddr->NeededGlitchBits = 2;
    //   else
    //     pinDbncAddr->NeededGlitchBits = calcGlitchBits;
    // }

    // syslog(1, "upd-(cfg)-0x%02x cfg->glitchFilterCycleCount:%d\n", cfg->glitchFilterCycleCount);

    // none = 0, rising = 1, falling = 2 & both = 3
    // Must match F7GPIOManager_interrupts.cs in WireInterrupt()
    pinDbncAddr->InterruptMode = cfg->risingEdge | cfg->fallingEdge << 1;
    syslog(1, "upd-(cfg)-0x%02x (P%c%d)--debounceDuration:%d, DebounceMask:0x%08x, DebounceDivisor:%d, ticksNeeded:%d\n",
              pinDbncAddr->PinId,
              ((pinDbncAddr->PinId) >> 4) + 'A', pinDbncAddr->PinId & 0x0f,
              cfg->debounceDuration,
              pinDbncAddr->DebounceMask,
              pinDbncAddr->DebounceDivisor,
              ticksNeeded);

    // Tell Nuttx how to handle interrupts
    ret = stm32_gpiosetevent(
    pinDbncAddr->PinId,
    // Because of switch bounce we need to tell Nuttx we need both rising and falling
    1,                    // risingEdge,
    1,                    // fallingEdge,
    0,                    // event
    upd_gpio_interrupt,   // func
    pinDbncAddr);
    
    pinDbncAddr->DebounceStateEnum = dbse_config;
    numbDebounceConfigured++;
  }
  else
  {
    numbDebounceConfigured--;    
    pinDbncAddr->DebounceStateEnum = dbse_unconfig;

    // Tell Nuttx to forget about interrupts
    ret = stm32_gpiosetevent(
        pinDbncAddr->PinId,
        false, false, 0, NULL, NULL);
  }
  return ret;
}
