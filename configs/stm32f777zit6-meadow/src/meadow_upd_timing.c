/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\meadow-upd-interrupt.c
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

#define MEADOW_UPD_TIMING_DIAGNOSTIC_SYSLOG (0)    // 0 > will include

/****************************************************************************
 * Private Types
 ****************************************************************************/
enum updTimingProcState_e
{
  updtpstate_uncfg,           // not configured
  // updtpstate_wait_gpio_isr,   // waiting for an interrupt from a gpio
  // updtpstate_mon_glitch,      // gpio is being monitored for glitch
  // updtpstate_mon_debounce,    // gpio is being monitored for debounce
  // updtpstate_mon_no_delay     // gpio is being monitored with no glitch or debounce delay
};

enum RequestedTimingMode_e
{
  updtrm_none,
  updtrm_rising,
  updtrm_falling,
  updtrm_both
};

// The following struct stores the informtion needed for debounce
struct timingPinMap_s
{
  // Represents the CPU Port and Pin identifier (e.g. PD9, D=3 so > '39')
  const uint8_t PortId;

  // The Timer's base register address
  const uint32_t TimerBaseAddress;

  // This is the number TIM1 - TIM14
  uint8_t TimerNumber;

  // Which timer input channel is being used (1-4)?
  uint8_t TimerInputChannel;

  // Different timers get there counting clock from APB1 or APB2 and therefore
  // need a different divider.
  uint8_t ABPClockDivider;

  // Dynamic data follows
  // CurrentProcessState - tracks the current processing state for this GPIO
  // defined by an entry in updTimingProcState_e enum
  uint8_t CurrentProcessState;

  // THIS IS TEMPORARY UNTIL THE USE OF NUTTX RUNTIME CODE IS REPLACED WITH
  // TABLE ENTRIES ETC.
  struct stm32_tim_dev_s *NuttxTimerStruct;

// Only if decide to have a separate ISR for each timer
//  xcpt_t isrHandler; isrHandler = upd_periodic_timeout_isr;
};

// Each row represents one Meadow GPIO. Only those GPIOs that are usable are listed
static struct timingPinMap_s timingData_s[] =
{
//                        |<------Fixed by design------>|<- runtime data -->
// Off name Port     Port  TimBasAddress    TIM# TIC  CLD  Current State    NTX
/* 00  A02  PA3 */  {0x03, STM32_TIM9_BASE,   9,  2,  48,  updtpstate_uncfg, 0, },
/* 01  D03  PB8 */  {0x19, STM32_TIM10_BASE, 10,  1,  48,  updtpstate_uncfg, 0, },
/* 02  D04  PB9 */  {0x27, STM32_TIM11_BASE, 11,  1,  48,  updtpstate_uncfg, 0, },
};

#define MEADOW_UPD_NUMB_TIMERS_SUPPORTED (3)

// // All NVIC interrupt assigments
// // Note TIM1/TIM8 are advanced-control timers and TIM8_CH1 (D02) and TIM8_CH4 (D11) are used
// // #define STM32_IRQ_TIM1BRK     (STM32_IRQ_FIRST + 24) /* 24: TIM1 Break interrupt */
// // #define STM32_IRQ_TIM1UP      (STM32_IRQ_FIRST + 25) /* 25: TIM1 Update interrupt */
// // #define STM32_IRQ_TIM1TRGCOM  (STM32_IRQ_FIRST + 26) /* 26: TIM1 Trigger and Commutation interrupts */
// // #define STM32_IRQ_TIM1CC      (STM32_IRQ_FIRST + 27) /* 27: TIM1 Capture Compare interrupt */
// // #define STM32_IRQ_TIM2        (STM32_IRQ_FIRST + 28) /* 28: TIM2 global interrupt */
// // #define STM32_IRQ_TIM3        (STM32_IRQ_FIRST + 29) /* 29: TIM3 global interrupt */
// // #define STM32_IRQ_TIM4        (STM32_IRQ_FIRST + 30) /* 30: TIM4 global interrupt */
// // #define STM32_IRQ_TIM5        (STM32_IRQ_FIRST + 50) /* 50: TIM5 global interrupt */
// // #define STM32_IRQ_TIM6        (STM32_IRQ_FIRST + 54) /* 54: TIM6 global interrupt */
// // #define STM32_IRQ_TIM7        (STM32_IRQ_FIRST + 55) /* 55: TIM7 global interrupt */
// // #define STM32_IRQ_TIM8BRK     (STM32_IRQ_FIRST + 43) /* 43: TIM8 Break interrupt */
// // #define STM32_IRQ_TIM8UP      (STM32_IRQ_FIRST + 44) /* 44: TIM8 Update interrupt */
// // #define STM32_IRQ_TIM8TRGCOM  (STM32_IRQ_FIRST + 45) /* 45: TIM8 Trigger and Commutation interrupts */
// // #define STM32_IRQ_TIM8CC      (STM32_IRQ_FIRST + 46) /* 46: TIM8 Capture Compare interrupt */
// // #define STM32_IRQ_TIM9        (STM32_IRQ_FIRST + 24) /* 24: TIM9 global interrupt */
// // #define STM32_IRQ_TIM10       (STM32_IRQ_FIRST + 25) /* 25: TIM10 global interrupt */
// // #define STM32_IRQ_TIM11       (STM32_IRQ_FIRST + 26) /* 26: TIM11 global interrupt */
// // #define STM32_IRQ_TIM12       (STM32_IRQ_FIRST + 43) /* 43: TIM12 global interrupt */
// // #define STM32_IRQ_TIM13       (STM32_IRQ_FIRST + 44) /* 44: TIM13 global interrupt */
// // #define STM32_IRQ_TIM14       (STM32_IRQ_FIRST + 45) /* 45: TIM14 global interrupt */


// static struct timingPinMap_s *gpiosBeingTimed[MEADOW_UPD_NUMB_TIMERS_SUPPORTED];
// static int timedGpioPinCount;    // Independent from totalGpiosBeingTimed

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int upd_timing_config_timer(struct upd_timing_config_s* cfg);
static int upd_periodic_timeout_isr(int irq, void *context, void *arg);

// static int upd_gpio_interrupt(int irq, void *context, void *arg);
// static int upd_config_interrupt_prep_timer(int stm32_timer_numb);
// static inline int upd_forward_interrupt_to_core(struct timingPinMap_s *timerMapTblPtr, uint8_t state);
// static int upd_process_gpio_debounce(struct timingPinMap_s *timerMapTblPtr);
// static int upd_process_gpio_glitch(struct timingPinMap_s *timerMapTblPtr);
// static int upd_meadow_debounce_notification_logic(struct timingPinMap_s *timerMapTblPtr);
// static inline uint8_t upd_read_current_gpio_state(struct timingPinMap_s *timerMapTblPtr);
// static void upd_add_gpio_to_timed_list(struct timingPinMap_s *timerMapTblPtr);
// static int upd_remove_gpio_to_timed_list(struct timingPinMap_s *timerMapTblPtr);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool _firstTimeConfig = true;

// For Testing use these GPIOs
#define TEST_GPIO_OUTPUT_A0  (GPIO_OUTPUT|GPIO_PORTA|GPIO_PIN4|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)
#define TEST_GPIO_OUTPUT_A1  (GPIO_OUTPUT|GPIO_PORTA|GPIO_PIN5|GPIO_FLOAT|GPIO_PUSHPULL|GPIO_SPEED_100MHz)

/****************************************************************************
 * Private Functions
 ****************************************************************************/
//===============================================================
// This function is called every x microseconds
static int upd_periodic_timeout_isr(int irq, void *context, void *arg)
{
  struct timingPinMap_s *timerMapTblPtr = (struct timingPinMap_s *)arg;

  stm32_gpiowrite(TEST_GPIO_OUTPUT_A0, true);
  
  // Acknowledge timer interrupt.
  STM32_TIM_ACKINT(timerMapTblPtr->NuttxTimerStruct, GTIM_SR_UIF);

  // Toggle GPIO so we can measure frequency as 2 * gpio
  stm32_gpiowrite(TEST_GPIO_OUTPUT_A0, false);
  return OK;
}

//====================================================================
static int upd_config_timer_input_capture(struct timingPinMap_s *timerMapTblPtr)
{
  // Note: the managed side has already configured the GPIO as needed
  int ret = OK;
  uint32_t offset = 0;
  uint32_t CCxSshift;
  uint32_t ICxFshift;
  uint32_t CCERshift;
  
  // Set capture/compare mode register

  // TIMx_CCMR1 register for TI1 & TI2, TIMx_CCMR2 register for TI3 & TI4
  // To keep things simple we'll use TI1 to CCR1 etc, and not support TI1 to CCR2.
  // Bit are always '01' but which register and what bits?

  // Assume a nice clean input so no fitering is needed
  switch(timerMapTblPtr->TimerInputChannel)
  {
      case 1:
        offset = STM32_GTIM_CCMR1_OFFSET;
        CCxSshift = 0;    // Capture/Compare 1 Selection
        ICxFshift = 4;    // Input Capture 1 Filter
        CCERshift = 0;    // Capture/Compare 1 Enable Register
        break;

      case 2:
        offset = STM32_GTIM_CCMR1_OFFSET;
        CCxSshift = 8;
        ICxFshift = 12;
        CCERshift = 4;
        break;
      
      case 3:
        offset = STM32_GTIM_CCMR2_OFFSET;
        CCxSshift = 0;
        ICxFshift = 4;
        CCERshift = 8;
        break;

      case 4:
        offset = STM32_GTIM_CCMR2_OFFSET;
        CCxSshift = 8;
        ICxFshift = 12;
        CCERshift = 12;
        break;
      
      default:
        return ERROR;
  }

  // Set CCMRx register with correct capture/compare mode selection
  // 1. Select the active input: TIMx_CCR1 must be linked to the TI1 input, so write the CC1S
  // bits to ‘01’ in the TIMx_CCMR1 register (for TI2 set '01' in CC2S). As soon as CC1S
  // becomes different from ‘00’, the channel is configured in input mode and the TIMx_CCR1
  // register becomes readonly.
  modifyreg16(timerMapTblPtr->TimerBaseAddress + offset, 0x0003 << CCxSshift, 0x0002 << CCxSshift);

  // 2. Program the input filter duration you need with respect to the signal you connect to the
  // timer (by programming the ICxF bits in the TIMx_CCMRx register if the input is one of
  // the TIx inputs). Let’s imagine that, when toggling, the input signal is not stable during at
  // must 5 internal clock cycles. We must program a filter duration longer than these 5
  // clock cycles. We can validate a transition on TI1 when 8 consecutive samples with the
  // new level have been detected (sampled at fDTS frequency). Then write IC1F bits to
  // ‘0011’ in the TIMx_CCMR1 register.

  // Assuming a nice clean signal. So, set CCMRx register with correct input filter, which is none
  modifyreg16(timerMapTblPtr->TimerBaseAddress + offset, 0x0004 << ICxFshift, 0x0000);

  // 3. Select the edge of the active transition on the TI1 channel by programming CC1P and
  // CC1NP bits to ‘00’ in the TIMx_CCER register (rising edge in this case).
  uint16_t regval;
  uint16_t ccer_en_bit;   // 0 = input capture disabled, 1 = enabled

  // timerMapTblPtr->InputInterruptMode: None = 0, Rising = 1, Falling = 2 and Both = 3;
  // This involves 3 bits accros a 4 bit field
  switch (timerMapTblPtr->InputInterruptMode)
  {
    case 0:
      regval = 0;
      ccer_en_bit = 0;    // isable
      break;

    case 1:
      regval      = 0;    // 00 = rising input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    case 2:
      regval      = GTIM_CCER_CC1P; // falling input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    case 3:
      regval      = GTIM_CCER_CC1P | GTIM_CCER_CC1NP; // rising and falling input
      ccer_en_bit = GTIM_CCER_CC1E; // enable
      break;

    default:
      return ERROR;
  }

  //                      0                1               4
  uint16_t mask = (GTIM_CCER_CC1E | GTIM_CCER_CC1P | GTIM_CCER_CC1NP);
  mask <<= CCERshift;
  regval <<= CCERshift;
  ccer_en_bit <<= CCERshift;    // Set last
  
  // Set rising and or falling or both
  stm32_modifyreg16(timerMapTblPtr->TimerBaseAddress + STM32_GTIM_CCER_OFFSET, mask, regval);

  // 5. Enable capture from the counter into the capture register by setting the CC1E bit in the
  // TIMx_CCER register.
  stm32_modifyreg16(timerMapTblPtr->TimerBaseAddress + STM32_GTIM_CCER_OFFSET, 0, ccer_en_bit);

  // Start timer after every input capture setup
  // STM32_TIM_SETMODE(timerMapTblPtr->NuttxTimerStruct, STM32_TIM_MODE_UP);
}

//====================================================================
// Sets bits in the DMA/Interrupt enable register (TIMx_DIER)
static int upd_config_timer_interrupt_eanble(struct timingPinMap_s *timerMapTblPtr, bool enable)
{
  uint16_t mask;

  // Every timer has this register, which interrupt (1-4) needs to be enabled/disabled?
  switch(timerMapTblPtr->TimerInputChannel)
  {
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

  if(enable)
    stm32_modifyreg16(timerMapTblPtr->TimerBaseAddress + STM32_GTIM_DIER_OFFSET, 0, mask);
  else
    stm32_modifyreg16(timerMapTblPtr->TimerBaseAddress + STM32_GTIM_DIER_OFFSET, mask, 0);

  return OK;
}

//====================================================================
static int upd_config_timer_frequency(struct timingPinMap_s *timerMapTblPtr)
{
  int ret = OK;
  uint32_t frequency;
  uint32_t period;

  // pwm-TODO: REPLACE THE NUTTX CALLS WITH MORE DIRECT METHODS
  timerMapTblPtr->NuttxTimerStruct = stm32_tim_init(timerMapTblPtr->TimerNumber);
  if(timerMapTblPtr->NuttxTimerStruct == NULL)
  {
    syslog(LOG_ERR, "%s@%d-stm32_tim_init returned NULL for TIM%d\n",
          __FILE__, __LINE__, timerMapTblPtr->TimerNumber);
    return -1;
  }

  // Try 1/4 of microsec ( 25 nanosec) clock rate
  frequency = STM32_APB2_TIM11_CLKIN/timerMapTblPtr->ABPClockDivider;  // 25 nanosec clock
  period = 65536 - 1;   // Max 16-bit overflow

  // This determines the prescaler value 0 - 65535
  // STM32_BTIM_PSC_OFFSET (PreScaler)
  STM32_TIM_SETCLOCK(timerMapTblPtr->NuttxTimerStruct, frequency);

  // Increasing period decreases the frequency
  // STM32_BTIM_ARR_OFFSET (AutoReloadRegister)
  STM32_TIM_SETPERIOD(timerMapTblPtr->NuttxTimerStruct, period);

  // third parameter is a pointer that's returned in the isr handler
  ret = STM32_TIM_SETISR(timerMapTblPtr->NuttxTimerStruct, upd_periodic_timeout_isr, timerMapTblPtr, 0);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-STM32_TIM_SETISR failed:%d\n",
          __FILE__, __LINE__, ret);
    return ret;
  }

  // Prevent interrupts until needed
  STM32_TIM_SETMODE(timerMapTblPtr->NuttxTimerStruct, STM32_TIM_MODE_DISABLED);

  // Finish set up
  STM32_TIM_ACKINT(timerMapTblPtr->NuttxTimerStruct, GTIM_SR_UIF);
  STM32_TIM_ENABLEINT(timerMapTblPtr->NuttxTimerStruct, GTIM_DIER_UIE);

  return ret;
}

//===============================================================
// Called during Nuttx startup
int meadow_upd_timing_setup(void)
{
  int ret;
  upd_timing_config_s timing_config;

  // Setup test GPIO
  ret = stm32_configgpio(TEST_GPIO_OUTPUT_A0);
  ret = stm32_configgpio(TEST_GPIO_OUTPUT_A1);

  //-------------------------------------------
  // EVERYTHING BELOW IS TEMPORARY FOR TESTING ALL 3 TIMERS
  // Setup test timer (just timer number TIM11 is 11)
  timing_config.enable = 1;              // 1 = enable
  timing_config.risingEdge = 1;          // 1 = enable
  timing_config.fallingEdge = 0;         // 1 = enable
  timing_config.resistorMode = 2;        // 0 = float, 1 = pull up, 2 = pull down
  
  // A02 (PA3)
  timing_config.port = 0;                // 0 - 15
  timing_config.pin = 3;                 // 0 - 15
  ret = upd_timing_config_timer(&timing_config);
  
  // D03 (PB8)
  timing_config.port = 1;                // 0 - 15
  timing_config.pin = 8;                 // 0 - 15
  ret = upd_timing_config_timer(&timing_config);

  // D04 (PB9)
  timing_config.port = 1;                // 0 - 15
  timing_config.pin = 9;                 // 0 - 15
  ret = upd_timing_config_timer(&timing_config);
  return OK;
}

//===============================================================
// Configure is called from managed code via upd
int upd_timing_config_timer(struct upd_timing_config_s* cfg)
{
  struct timingPinMap_s *timerMapTblPtr;
  uint32_t designator = cfg->port << 4 | cfg->pin;
  uint32_t pinMapOffset;
  int ret;

  // Walk the timer data array to find the desired entry
  for(pinMapOffset = 0; pinMapOffset < MEADOW_UPD_NUMB_TIMERS_SUPPORTED; pinMapOffset++)
  {
    if(timingPinMap_s[pinMapOffset].PortId == designator)
      break;
  }

  if(pinMapOffset == MEADOW_UPD_NUMB_TIMERS_SUPPORTED)
  {
    // Timer not found in data table? This is not expected.
    syslog(LOG_ERR, "upd-timer cfg--No entry in table for 0x%02x\n", designator);
    return -1;
  }

  // Grab the correct entry's table address
  timerMapTblPtr = &timingPinMap_s[pinMapOffset];
  timerMapTblPtr->InputInterruptMode = cfg->risingEdge & 0x01) | (cfg->fallingEdge & 0x01) << 1;

  if(_firstTimeConfig)
  {
    // NEEDED?
    // Only do this once, the first time
    // ret = upd_config_interrupt_prep_timer();
    // if(ret < 0)
    // {
    //   syslog(LOG_ERR, "upd-timer cfg---upd_config_interrupt_prep_timer failed\n");
    //   return -1;
    // }

    // Initialize table
    // for(int i = 0; i < MEADOW_UPD_NUMB_TIMERS_SUPPORTED; i++)
    // {
    //   timingPinMap_s[i] = NULL;
    // }

    _firstTimeConfig = false;
  }

  if(cfg->enable)
  {
    // Configure the appropriate timer
    ret = upd_config_timer_frequency(timerMapTblPtr);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-upd_config_timer_frequency failed:%d\n",
            __FILE__, __LINE__, ret);
      return ret;
    }

    ret = upd_config_timer_input_capture(timerMapTblPtr);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-upd_config_timer_input_capture failed:%d\n",
            __FILE__, __LINE__, ret);
      return ret;
    }

    ret = upd_config_timer_interrupt_eanble(timerMapTblPtr, true);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-upd_config_timer_interrupt_eanble failed:%d\n",
            __FILE__, __LINE__, ret);
      return ret;
    }
    
    

    // cfgset contains 20-bits of data. It is required by the Nuttx stm32_gpiosetevent
    // function. If the 20 bits of data are not correct, this Nuttx function will
    // reconfigure the GPIO based on whatever the data is cfgset.
    // See stm32_gpio.h for more information.
    // Inputs: MMUU .... ...X PPPP BBBB
    // MM = Mode for input (this is 00)
    // UU = pull up, pull down or float
    // X  = is external interrupt selection, stm32_gpiosetevent sets this
    uint32_t cfgset = (designator & 0x000000ff);   // Set Port and Pin and clear MM

    switch(cfg->resistorMode)
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

#if MEADOW_UPD_INCLUDE_DIAGNOSTIC_SYSLOG > 0
    syslog(LOG_INFO, "upd-(cfg)- 0x%02x (P%c%d)-Cfg Enabled-LKS:%d, GLDuration:%d, DBDuration:%d, InterruptMode:%d, cfgset:0x%08x\n",
              timerMapTblPtr->PortId,
              ((timerMapTblPtr->PortId) >> 4) + 'A', timerMapTblPtr->PortId & 0x0f,
              timerMapTblPtr->LastKnownGpioState,
              timerMapTblPtr->GlitchRequestedDuration,
              timerMapTblPtr->DebounceRequestedDuration,
              timerMapTblPtr->GpioInterruptMode,
              cfgset);
#endif

    upd_config_timer_interrupt_eanble(timerMapTblPtr, true);
  } // if(cfg->enable)
  else
  {
    // Disable timing
    #if MEADOW_UPD_INCLUDE_DIAGNOSTIC_SYSLOG > 0
    syslog(LOG_INFO, "udp-(cfg)-0x%02x (P%c%d)--Disabling GPIO\n", timerMapTblPtr->PortId,
                ((timerMapTblPtr->PortId) >> 4) + 'A', timerMapTblPtr->PortId & 0x0f);
    #endif
    upd_config_timer_interrupt_eanble(timerMapTblPtr, false);
    timerMapTblPtr->CurrentProcessState = updipstate_uncfg;
  }

  return ret;
}
