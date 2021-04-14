/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\meadow-upd-measurefreq.c
 * 
 *   Copyright (C) 2020, 2021 Wilderness Labs. All rights reserved.
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
// #include <nuttx/spi/spi.h>

#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "chip.h"
#include "fcntl.h"
//#include "stm32_pwm.h"
//#include "stm32_i2c.h"
#include "stm32f777zit6-meadow.h"
#include "stm32_spi.h"

#include <dirent.h>

#include <sys/ioctl.h>
#include <nuttx/timers/timer.h>
#include "stm32_tim.h"

#include <nuttx/clock.h>    // for testing

#include "meadow-upd.h"

// FOR TESTING
#include "stm32_gpio.h"

#define MEADOW_UPD_MFREQ_DIAGNOSTIC_SYSLOG (0)    // 0 > will include

// See AN4013 STM32 cross-series timer overview

/****************************************************************************
 * Private Types
 ****************************************************************************/
enum updMFreqProcState_e
{
  updmfstate_uncfg,           // not configured
  // updmfstate_wait_gpio_isr,   // waiting for an interrupt from a gpio
  // updmfstate_mon_glitch,      // gpio is being monitored for glitch
  // updmfstate_mon_debounce,    // gpio is being monitored for debounce
  // updmfstate_mon_no_delay     // gpio is being monitored with no glitch or debounce delay
};

// The following struct stores the informtion needed for debounce
struct mfreqPinMap_s
{
  // Represents the F7s Port and Pin identifier (e.g. PD9, D=3 so > '39')
  // dedicated for each timer
  const uint8_t PortId;

  // Alternate function to configure GPIO input point
  const uint32_t GpioInputAltFunc;

  // The Timer's base register address & IRQ number (NVIC offset)
  const uint32_t TimerBaseAddress;    // Tim11 is 0x40014800 (STM32_TIM11_BASE)
  const uint32_t NvicInterruptOffset; // Tim11 is 42 (STM32_IRQ_TIM11)
  
  // Capture and compare register offset
  const uint32_t CapCmpRegister;    // TIMx_CCRx

  // This is the number TIM1 - TIM14
  uint8_t TimerNumber;                // Tim11 is 11

  // Input channel used for the related GPIO 
  uint8_t TimerInputChannel;    // 1 - 4

  // APB Clock Divider
  // Timers get their clock from APB1 or APB2. Timers 1, 8, 9, 10 & 11 run off
  // the APB2 clock while the others the APB1 clock. The APB2 clock runs twice
  // as fast as the APB1. At this time APB2 is at 1,920,000Hz and APB1 is 960,000Hz.
  // See board.h CONFIG_STM32F7_OTGFS currently defines the frequencies.
  //
  // The goal is to have every timer count every 250 nano second
  uint8_t ABPxClockFactor;

  //-----------------------------------------------
  // Dynamic data follows

  // CurrentProcessState - tracks the current processing state for this GPIO
  // defined by an entry in updMFreqProcState_e enum
  uint8_t CurrentProcessState;

// Only needed if decide to have a separate ISR for each timer
//  xcpt_t isrHandler;
};

// Each row represents one Meadow GPIO. Only those GPIOs that are usable are listed
// Note: At least for now - only Meadow exposed GPIOs that can be configured as an
// input channel of a Timer that is NOT used for PWM are being considered.
// WARNING: Tim1 & Tim8 have not been tested and may not work without modifications.
// While not tested timers 2 & 5 which have 32-bit registers might work.
// Note: Tim6 & Tim7 have no input capture ability and cannot be used. 
static struct mfreqPinMap_s mfreqDataArray[] =
{
// Off WLab         |<-----------                              Fixed by design                 ----------->|<-- runtime data -->
// set Name Port     Port   AltFunc   TimBasAddress    Timer IRQ Numb   Capture & Compare Reg  TIM# TIC ACD  Current State   
/* 00  A02  PA3 */  {0x03, GPIO_AF3, STM32_TIM9_BASE,  STM32_IRQ_TIM9,  STM32_GTIM_CCR2_OFFSET,  9,  2,  48, updmfstate_uncfg },
/* 01  D03  PB8 */  {0x18, GPIO_AF3, STM32_TIM10_BASE, STM32_IRQ_TIM10, STM32_GTIM_CCR1_OFFSET, 10,  1,  48, updmfstate_uncfg },
/* 02  D04  PB9 */  {0x19, GPIO_AF3, STM32_TIM11_BASE, STM32_IRQ_TIM11, STM32_GTIM_CCR1_OFFSET, 11,  1,  48, updmfstate_uncfg },
};

#define MEADOW_UPD_NUMB_TIMERS_SUPPORTED (3)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool _firstTimeConfig = true;

// For Testing use these GPIOs
#define TEST_GPIO_OUTPUT_A0  (GPIO_OUTPUT|GPIO_PORTA|GPIO_PIN4|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)
#define TEST_GPIO_OUTPUT_A1  (GPIO_OUTPUT|GPIO_PORTA|GPIO_PIN5|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)
#define TEST_GPIO_OUTPUT_A3  (GPIO_OUTPUT|GPIO_PORTA|GPIO_PIN7|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)
#define TEST_GPIO_OUTPUT_A4  (GPIO_OUTPUT|GPIO_PORTC|GPIO_PIN0|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)
#define TEST_GPIO_OUTPUT_A5  (GPIO_OUTPUT|GPIO_PORTC|GPIO_PIN1|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This function is called whenever a timer event has occured
static int upd_mfreq_interrupt_callback(int nvicOffset, void *context, void *arg)
{
  // nvicOffset: timer's offset into NVIC interrupt table
  // context: pointer to area on stack where registers were already saved
  // arg: table pointer (user supplied value when registering interrupt)

  // Acknowledge timer interrupt. Fortunately at this point we don't
  // care about which GPIO this is for.

  struct mfreqPinMap_s *mfreqArrayPtr = (struct mfreqPinMap_s *)arg;
  //syslog(1, "---> Callback-table:%p, nvicOffset:%d (expect 42)\n", mfreqArrayPtr, nvicOffset);
  DEBUGASSERT(mfreqArrayPtr->NvicInterruptOffset == nvicOffset);

  // Measure the perf
  stm32_gpiowrite(TEST_GPIO_OUTPUT_A0, true);
  uint32_t interruptEvent;

  interruptEvent = getreg32(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
  if(interruptEvent & GTIM_SR_UIF)
  {
    // Update interrupt flag (overflow or underflow of counter)
    // DON'T PUT SYSLOG HERE IT'S CALLED TOO OFTEN
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A1, true);
    putreg16(interruptEvent & ~GTIM_SR_UIF, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A1, false);
  }
  else if (interruptEvent & GTIM_SR_CC1IF)
  {
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A3, true);
    syslog(1, "==> ISR-CC1IF (channel 1 interrupt detected)\n");
    // Capture/compare 1 interrupt flag
    putreg16(interruptEvent & ~GTIM_SR_CC1IF, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A3, false);
  }
  else if (interruptEvent & GTIM_SR_CC2IF)
  {
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
    syslog(1, "==> ISR-GTIM_SR_CC2IF (channel 2 interrupt detected)\n");
    putreg16(interruptEvent & ~GTIM_SR_CC2IF, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
  }
  else if (interruptEvent & GTIM_SR_CC3IF)
  {
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
    syslog(1, "==> ISR-GTIM_SR_CC3IF (channel 3 interrupt detected)\n");
    putreg16(interruptEvent & ~GTIM_SR_CC3IF, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
  }
  else if (interruptEvent & GTIM_SR_CC4IF)
  {
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
    syslog(1, "==> ISR-GTIM_SR_CC4IF (channel 4 interrupt detected)\n");
    putreg16(interruptEvent & ~GTIM_SR_CC4IF, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);
    stm32_gpiowrite(TEST_GPIO_OUTPUT_A4, true);
  }
  else
  {
    // ???
    syslog(1, "---> Callback-Unknown interrupt:0x%08x,nvicOffset:%d \n", interruptEvent, nvicOffset);
  }

// mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CNT_OFFSET

  // Tim2 & 5 are 32-bits all others are 16
  // uint32_t regValue;
  // if (mfreqArrayPtr->TimerNumber == 2 || mfreqArrayPtr->TimerNumber == 5)
  // {
  //   regValue = getreg32(mfreqArrayPtr->TimerBaseAddress + mfreqArrayPtr->CapCmpRegister);
  // }

  // regValue = getreg16(mfreqArrayPtr->TimerBaseAddress + mfreqArrayPtr->CapCmpRegister);
  // syslog(1, "---> Measured count:%d\n", regValue);

  // // Re-enable this interrupt
  // up_enable_irq(nvicOffset);

  // End perf
  stm32_gpiowrite(TEST_GPIO_OUTPUT_A0, false);
  return OK;
}

//=======================================================================
// This uses Nuttx to do the routing
static int upd_mfreq_set_callback(struct mfreqPinMap_s *mfreqArrayPtr,
                xcpt_t handler, void *arg)
{
  // Use Nuttx at least for now
  struct stm32_tim_dev_s *tempTimer;
  int ret;

  // pwm - For TESTING
  tempTimer = stm32_tim_init(mfreqArrayPtr->TimerNumber);
  if(tempTimer == NULL)
  {
    syslog(LOG_ERR, "%s@%d-stm32_tim_init returned NULL\n",
          __FILE__, __LINE__);
    return OK;
  }

  // For future reference
  // -- for 1 microsec --
  // frequency = STM32_APB2_TIM11_CLKIN;        // = 192,000,000 Hz
  // period = 192 - 1;
  // -- for 1 millisec --
  // frequency = STM32_APB2_TIM11_CLKIN / 100; // = 1,920,000 Hz
  // period = 1920 - 1;                        // = 1 millisec  

  // This determines the prescaler value 0 - 65535
  // TIMx prescaler (STM32_GTIM_PSC_OFFSET)
  // STM32_APB2_TIM11_CLKIN = 192,000,000 Hz
  // uint32_t frequency = STM32_APB2_TIM11_CLKIN / 100;
  uint32_t frequency = STM32_APB2_TIM11_CLKIN/mfreqArrayPtr->ABPxClockFactor;
  STM32_TIM_SETCLOCK(tempTimer, frequency);

  // // Increasing period decreases the frequency
  // // STM32_GTIM_ARR_OFFSET (auto-reload register)
  uint32_t period = 65536 - 1;
  STM32_TIM_SETPERIOD(tempTimer, period);

  // arg (third parameter) is a pointer that's returned in the isr handler
  ret = STM32_TIM_SETISR(tempTimer, handler, arg, 0);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-STM32_TIM_SETISR failed:%d\n",
          __FILE__, __LINE__, ret);
    return ret;
  }

  // Prevent interrupts until needed
  STM32_TIM_SETMODE(tempTimer, STM32_TIM_MODE_DISABLED);

  // Finish set up
  STM32_TIM_ACKINT(tempTimer, GTIM_SR_UIF);
  STM32_TIM_ENABLEINT(tempTimer, GTIM_DIER_UIE);

  // Start running
  STM32_TIM_SETMODE(tempTimer, STM32_TIM_MODE_UP);

  // // The offset into the NVIC interrupt table
  // int nvicOffset = mfreqArrayPtr->NvicInterruptOffset;

  // syslog(1, "---> Setting Callback for nvicOffset:%lu\n", nvicOffset);

  // // Disable interrupt when callback is removed
  // if (!handler)
  //   {
  //     up_disable_irq(nvicOffset);
  //     irq_detach(nvicOffset);
  //     return OK;
  //   }

  // // Otherwise set callback and enable interrupt
  // irq_attach(nvicOffset, handler, arg);
  // up_enable_irq(nvicOffset);
  return OK;
}

//====================================================================
// Called when configuring input capture
static int upd_config_mfre_input_capture(struct mfreqPinMap_s *mfreqArrayPtr, uint32_t interruptMode)
{
  // Setup some basic timing.
  
  // Clear the direction bit (0=count up) and select the Counter Mode (0=Edge aligned)
  uint16_t cr1 = getreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CR1_OFFSET);
  cr1 &= ~(GTIM_CR1_DIR | GTIM_CR1_CMS_MASK);
  putreg16(cr1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CR1_OFFSET);
  
  // // Set the counting rate for this timer
  // // pwm - THIS NEEDS TO BE FIXED only supports 3 hardcoded timers
  // uint32_t ClockInput;
  // switch(mfreqArrayPtr->TimerNumber)
  // {
  //   case 9:
  //     ClockInput = STM32_APB2_TIM9_CLKIN;
  //     // RCC_APB2ENR = APB2 peripheral reset register
  //     // Enable clock for this timer
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM9EN);
  //     break;
  //   case 10:
  //     ClockInput = STM32_APB2_TIM10_CLKIN;
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM10EN);
  //     break;
  //   case 11:
  //     ClockInput = STM32_APB2_TIM11_CLKIN;
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM11EN);
  //     break;
  //   default:
  //   DEBUGASSERT(false); // Undefined timer in use
  // }

  // // Auto-Reload Register is use to establish the count at which the timer's counting
  // // restarts. It works with the timer's PSC register to establish the time base
  // if (mfreqArrayPtr->TimerNumber == 2 || mfreqArrayPtr->TimerNumber == 5)
  //   putreg32(960 - 1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_ARR_OFFSET);
  // else
  //   putreg16(960 - 1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_ARR_OFFSET);

  // // Use this to set the prescaler STM32_GTIM_PSC_OFFSET to slow down the native clock
  // uint32_t timerClock = ClockInput/mfreqArrayPtr->ABPxClockFactor;
  // putreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_PSC_OFFSET, timerClock);

  // ----------------------------------------------------------------------------
  // The following numbered steps were taken from the STM RM0410 Reference Manual, Rev 4,
  // section 26.3.5. Info within square braces [this is added by Peter]
  // ----------------------------------------------------------------------------
  // The following example shows how to capture the counter value in TIMx_CCR1 when TI1
  // input rises. To do this, use the following procedure:
  // 1. Select the active input: TIMx_CCR1 must be linked to the TI1 input, so write the CC1S
  //  bits to 01 in the TIMx_CCMR1 register. As soon as CC1S becomes different from 00,
  //  the channel is configured in input and the TIMx_CCR1 register becomes read-only.
  // 2. Program the input filter duration you need with respect to the signal you connect to the
  //  timer (when the input is one of the TIx (ICxF bits in the TIMx_CCMRx register). Let’s
  //  imagine that, when toggling, the input signal is not stable during at must 5 internal clock
  //  cycles. We must program a filter duration longer than these 5 clock cycles. We can
  //  validate a transition on TI1 when 8 consecutive samples with the new level have been
  //  detected (sampled at fDTS frequency). Then write IC1F bits to 0011 in the
  //  TIMx_CCMR1 register.
  // 3. Select the edge of the active transition on the TI1 channel by writing the CC1P and
  //  CC1NP and CC1NP bits to 000 in the TIMx_CCER register (rising edge in this case).
  // 4. Program the input prescaler. In our example, we wish the capture to be performed at
  //  each valid transition, so the prescaler is disabled (write IC1PS bits to 00 in the
  //  TIMx_CCMR1 register).
  // 5. Enable capture from the counter into the capture register by setting the CC1E bit in the
  //  TIMx_CCER register.
  // 6. If needed, enable the related interrupt request by setting the CC1IE bit in the
  //  TIMx_DIER register, and/or the DMA request by setting the CC1DE bit in the
  //  TIMx_DIER register. [This is done in a separate function upd_config_mfre_interrupt_enable()]
  // ----------------------------------------------------------------------------

  uint32_t ccmrOff = 0; // Capture/compare mode register
  uint32_t CCxSshift;   // capture/compare mode selection bits
  uint32_t ICxFshift;
  uint32_t ICxPSCshift;
  uint32_t CCERshift;

  // Based on timer input channel (1-4) we get the proper values
  switch(mfreqArrayPtr->TimerInputChannel)
  {
      case 1:
        // Note: for timers with 4 input channels CCMR1 controls channels 1 & 2 and CCMR2
        // controls channels 3 & 4
        CCxSshift = 0;    // (CC1S) Capture/Compare 1 selection bit shift
        ICxFshift = 4;    // Input Capture 1 Filter
        ccmrOff = STM32_GTIM_CCMR1_OFFSET;  // Capture/Compare Mode Register
        ICxPSCshift = 2;  // Input capture prescaler
        CCERshift = 0;    // Capture/Compare 1 Enable Register
        break;

      case 2:
        CCxSshift = 8;    // (CC2S)
        ICxFshift = 12;
        ccmrOff = STM32_GTIM_CCMR1_OFFSET;
        ICxPSCshift = 10;
        CCERshift = 4;
        break;
      
      // Only timer with > 2 input channels have CCMR2
      case 3:
        CCxSshift = 0;    // (CC3S)
        ICxFshift = 4;
        ccmrOff = STM32_GTIM_CCMR2_OFFSET;
        ICxPSCshift = 2;
        CCERshift = 8;
        break;

      case 4:
        CCxSshift = 8;    // (CC4S)
        ICxFshift = 12;
        ccmrOff = STM32_GTIM_CCMR2_OFFSET;
        ICxPSCshift = 10;
        CCERshift = 12;
        break;
      
      default:
        DEBUGASSERT(false);   // can't have more than 4 channels
  }

  // 1. Select the active input
  // [The following always maps Channelx to TIx, eventhough Channel1 could be mapped to TI2 and
  // Channel2 could be mapped to TI1 (also true for TI3 & TI4)]
  modifyreg16(mfreqArrayPtr->TimerBaseAddress + ccmrOff, 0x0003 << CCxSshift, 0x0001 << CCxSshift);

  // 2. Program the input filter duration 
  // Assuming a nice clean signal (from a Hall-effect sensor).
  // So, set CCMRx register with correct input filter, which is none
  modifyreg16(mfreqArrayPtr->TimerBaseAddress + ccmrOff, 0x0007 << ICxFshift, 0x0000);

  uint16_t edgeValue;
  uint16_t ccer_en_bit;   // 0 = input capture disabled, 1 = enabled

  // This involves 3 bits across a 4 bit field
  // interruptMode: None = 0, Rising = 1, Falling = 2 and Both = 3;
  switch (interruptMode)
  {
    case 0:
      edgeValue   = 0;
      ccer_en_bit = 0;    // disable interrupts
      break;

    case 1:
      edgeValue   = 0b00;           // rising input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    case 2:
      edgeValue   = 0b01;           // falling input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    case 3:
      edgeValue   = 0b11;           // rising and falling input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    default:
      DEBUGASSERT(false);
      return ERROR;
  }

  uint16_t mask = 0b1011;   // Only bits 3, 1-0 are used
  mask <<= CCERshift;       // Move mask into position for timer
  edgeValue <<= CCERshift;  // Move bits into position for timer

  // 3. Select the edge of the active transition
  modifyreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CCER_OFFSET, mask, edgeValue);

  // 4. Program the input prescaler. Note:INPUT prescaler not TIMER prescaler
  // This allows the number of transitions to be scaled 0b00=no prescaler, 0b01=capture every
  // 2 transitions, 0b10=capture every 4 transitions, 0b11=capture every 8 transitions.
  // We want all input transitions so set = ob00
  modifyreg16(mfreqArrayPtr->TimerBaseAddress + ccmrOff, 0x0003 << ICxPSCshift, 0x0000);

  // 5. Enable capture from the counter into the capture register by setting the CC1E bit in the
  // TIMx_CCER register.
  ccer_en_bit <<= CCERshift;
  modifyreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CCER_OFFSET, 0, ccer_en_bit);

  // Not a numbered step. Start the timer counting
  // Auto-reload preload enable and Counter enable
  cr1 = getreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CR1_OFFSET);
  cr1 |= (GTIM_CR1_CEN | GTIM_CR1_ARPE);
  putreg16(cr1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CR1_OFFSET);

  // Clear Timer counter
  putreg16(0, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_CNT_OFFSET);
  
  // // Set the counting rate for this timer. This takes 2 steps
    
  // // Auto-Reload Register is use to establish the count at which the timer's counting
  // // restarts. It works with the timer's PSC register to establish the time base
  // if (mfreqArrayPtr->TimerNumber == 2 || mfreqArrayPtr->TimerNumber == 5)
  //   putreg32(9600 - 1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_ARR_OFFSET);
  // else
  //   putreg16(9600 - 1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_ARR_OFFSET);

  // // pwm - THIS NEEDS TO BE FIXED only supports 3 hardcoded timers
  // uint32_t ClockInput;
  // switch(mfreqArrayPtr->TimerNumber)
  // {
  //   case 9:
  //     ClockInput = STM32_APB2_TIM9_CLKIN;
  //     // RCC_APB2ENR = APB2 peripheral reset register
  //     // Enable clock for this timer
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM9EN);
  //     break;
  //   case 10:
  //     ClockInput = STM32_APB2_TIM10_CLKIN;
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM10EN);
  //     break;
  //   case 11:
  //     ClockInput = STM32_APB2_TIM11_CLKIN;
  //     modifyreg32(STM32_RCC_APB2ENR, 0, RCC_APB2ENR_TIM11EN);
  //     break;
  //   default:
  //   DEBUGASSERT(false); // Undefined timer in use
  // }

  // // Use this to set the prescaler STM32_GTIM_PSC_OFFSET to slow down the native clock
  // uint32_t timerClock = ClockInput/mfreqArrayPtr->ABPxClockFactor;
  // putreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_PSC_OFFSET, timerClock);

  // Clear any pending interrupts
  cr1 = ~(GTIM_SR_UIF | GTIM_SR_CC1IF | GTIM_SR_CC2IF | GTIM_SR_CC3IF | GTIM_SR_CC4IF);
  putreg16(cr1, mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_SR_OFFSET);

  return OK;
}

//====================================================================
// Sets bits in the DMA/Interrupt enable register (TIMx_DIER)
// 6. If needed, enable the related interrupt request by setting the CC1IE bit in the
// TIMx_DIER register, and/or the DMA request by setting the CC1DE bit in the
// TIMx_DIER register.
static int upd_config_mfre_interrupt_enable(struct mfreqPinMap_s *mfreqArrayPtr, bool enable)
{
  uint16_t mask;

  // Every timer has one or more channels. The mfreq array contains the channel
  // related to the correct GPIO
  switch(mfreqArrayPtr->TimerInputChannel)
  {
    // Find the capture/Compare 1 interrupt enable bin within the DMA/Interrupt enable register
    // For the STM32F77x these are bits 1-4 all other bit are cleared. So, this code enables
    // interrupts and disables DMA etc.
    case 1:
      mask = ATIM_DIER_CC1IE;
      break;
    case 2:
      mask = ATIM_DIER_CC2IE;
      break;
    case 3:
      mask = ATIM_DIER_CC3IE;
      break;
    case 4:
      mask = ATIM_DIER_CC4IE;
      break;
  }

  // DMA/Interrupt enable register (TIMx_DIER).
  if(enable)
    modifyreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_DIER_OFFSET, 0, mask);
  else
    modifyreg16(mfreqArrayPtr->TimerBaseAddress + STM32_GTIM_DIER_OFFSET, mask, 0);

  return OK;
}


//===============================================================
// Called only once during Nuttx startup
int meadow_upd_freq_measure_setup(void)
{
  int ret;

  //-------------------------------------------
  // EVERYTHING BELOW IS TEMPORARY FOR TESTING
  // Setup diag GPIOs for testing
  stm32_configgpio(TEST_GPIO_OUTPUT_A0);
  stm32_configgpio(TEST_GPIO_OUTPUT_A1);
  stm32_configgpio(TEST_GPIO_OUTPUT_A3);
  stm32_configgpio(TEST_GPIO_OUTPUT_A4);
  stm32_configgpio(TEST_GPIO_OUTPUT_A5);

  // WHEN COMPLETE, EVERYTHING BELOW WILL BE REPLACED WITH CODE
  // ON THE MANAGED SIDE
  struct upd_mfreq_config_s timing_config;

  // Setup test timer configuration
  timing_config.enable = 1;              // 1 = enable
  timing_config.risingEdge = 1;          // 1 = enable
  timing_config.fallingEdge = 0;         // 1 = enable
  timing_config.resistorMode = 2;        // 0 = float, 1 = pull up, 2 = pull down
  
  // A02 (PA3)
  timing_config.port = 0;                // 0 - 15
  timing_config.pin = 3;                 // 0 - 15
  // Pretend to be managed side
  // ret = upd_config_freq_measure(&timing_config);
  
  // D03 (PB8)
  timing_config.port = 1;                // 0 - 15
  timing_config.pin = 8;                 // 0 - 15
  // Pretend to be managed side
  // ret = upd_config_freq_measure(&timing_config);

  // D04 (PB9)
  timing_config.port = 1;                // 0 - 15
  timing_config.pin = 9;                 // 0 - 15
  // Pretend to be managed side
  ret = upd_config_freq_measure(&timing_config);
  return ret;
}

//===============================================================
// Called when managed side is configuring an input to measure frequency
int upd_config_freq_measure(struct upd_mfreq_config_s* config)
{
  struct mfreqPinMap_s *mfreqArrayPtr;
  uint32_t designator = config->port << 4 | config->pin;
  uint32_t mfreqDataOffset;
  int ret;

  syslog(1, "---> Entered upd_config_freq_measure()\n");

  // One time programming
  if(_firstTimeConfig)
  {
    // NEEDED?
    // Only do this once, the first time
    for(mfreqDataOffset = 0; mfreqDataOffset < MEADOW_UPD_NUMB_TIMERS_SUPPORTED; mfreqDataOffset++)
    {
      struct mfreqPinMap_s *tempPtr = &mfreqDataArray[mfreqDataOffset];
      tempPtr->CurrentProcessState = updmfstate_uncfg;
    }
    _firstTimeConfig = false;
  }

  // Walk the timer mfreq array to find this GPIO entry
  for(mfreqDataOffset = 0; mfreqDataOffset < MEADOW_UPD_NUMB_TIMERS_SUPPORTED; mfreqDataOffset++)
  {
    if(mfreqDataArray[mfreqDataOffset].PortId == designator)
      break;
  }

  if(mfreqDataOffset == MEADOW_UPD_NUMB_TIMERS_SUPPORTED)
  {
    // Timer not found in data table? This is not expected.
    syslog(LOG_ERR, "upd-timer config--No entry in table for 0x%02x\n", designator);
    return -1;
  }

  // Save the correct entry's table address
  mfreqArrayPtr = &mfreqDataArray[mfreqDataOffset];

  if(config->enable)
  {
    syslog(1, "---> Configuring GPIO:0x%02x for timer:%u\n", designator, mfreqArrayPtr->TimerNumber);

    // cfgset contains 20-bits of data. It is required by the Nuttx stm32_gpiosetevent
    // function. If the 20 bits of data are not correct, this Nuttx function will
    // reconfigure the GPIO based on whatever the data is cfgset.
    // See stm32_gpio.h for more information.
    // Inputs: MMUU .... ...X PPPP BBBB
    // MM = Mode for input (this is 00)
    // UU = pull up, pull down or float
    // X  = is external interrupt selection, stm32_gpiosetevent sets this
    uint32_t cfgset = (designator & 0x000000ff);   // Keep Port and Pin, clear everything else

    // THIS MAY NOT BE NEEDED AS THE ALTERNATE FUNCTION MAY DO EVERYTHING NECESSARY???
    switch(config->resistorMode)
    {
      case 0: // Float
        cfgset |= GPIO_FLOAT;
        break;    // 0 = do nothing
      case 1: // Pull up
        cfgset |= GPIO_PULLUP;
        break;
      case 2: // Pull down
        cfgset |= GPIO_PULLDOWN;
        break;
    }

    // Set the GPIO. The alternate function will define which channel of
    // which timer this point is to be associated with for input.
    cfgset |= (GPIO_INPUT | mfreqArrayPtr->GpioInputAltFunc);
    stm32_configgpio(cfgset);

    // interruptMode: None = 0, Rising = 1, Falling = 2 and Both = 3;
    uint32_t interruptMode = (config->risingEdge & 0x01) | (config->fallingEdge & 0x01) << 1;
    ret = upd_config_mfre_input_capture(mfreqArrayPtr, interruptMode);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-upd_config_mfre_input_capture failed:%d\n",
            __FILE__, __LINE__, ret);
      return ret;
    }

    // Tell OS about our callback address and array pointer
    ret = upd_mfreq_set_callback(mfreqArrayPtr, upd_mfreq_interrupt_callback, mfreqArrayPtr);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-upd_mfreq_set_callback() failed:%d\n",
            __FILE__, __LINE__, ret);
      return ret;
    }

#if MEADOW_UPD_MFREQ_DIAGNOSTIC_SYSLOG > 0
    syslog(LOG_INFO, "upd-(mfreq config)- 0x%02x (P%c%d), cfgset:0x%08x\n",
              mfreqArrayPtr->PortId,
              ((mfreqArrayPtr->PortId) >> 4) + 'A', mfreqArrayPtr->PortId & 0x0f,
              cfgset);
#endif

    // Enable timing
    upd_config_mfre_interrupt_enable(mfreqArrayPtr, true);
  } // if(config->enable)
  else
  {
    // Disable timing

    // pwm - MORE CLEANUP NEEDED HERE FOR DISABLE
    #if MEADOW_UPD_MFREQ_DIAGNOSTIC_SYSLOG > 0
    syslog(LOG_INFO, "udp-(config)-0x%02x (P%c%d)--Disabling GPIO\n", mfreqArrayPtr->PortId,
                ((mfreqArrayPtr->PortId) >> 4) + 'A', mfreqArrayPtr->PortId & 0x0f);
    #endif

    upd_config_mfre_interrupt_enable(mfreqArrayPtr, false);
    mfreqArrayPtr->CurrentProcessState = updmfstate_uncfg;
  }

  syslog(1, "---> Exiting upd_config_freq_measure()\n");
  return ret;
}