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

#if defined CONFIG_SYSTEMTICK_HOOK
#include <nuttx/board.h>
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct upd_register_value
{
  uint32_t address;
  uint32_t value;
};

struct upd_register_update
{
  uint32_t address;
  uint32_t clearBits;
  uint32_t setBits;
};

struct upd_gpio_int_config
{
  uint32_t irq;
  uint32_t port;
  uint32_t pin;
  uint32_t enable;
  uint32_t risingEdge;
  uint32_t fallingEdge;
  uint32_t debounceMs;
};

struct upd_pwm_cmd
{
  uint32_t timer;
  uint32_t channel;
  uint32_t frequency;
  uint32_t duty;
};

struct upd_i2c_cmd
{
  uint32_t address;
  uint32_t frequency;
  uint8_t* txBuffer; // in to driver (so tx)
  uint32_t txLength;
  uint8_t* rxBuffer; // back out to app, so rx
  uint32_t rxLength;
};

struct upd_spi_data_cmd
{
  uint8_t* txBuffer; // in to driver (so tx)
  uint8_t* rxBuffer; // back out to app, so rx
  uint32_t length;
  uint32_t busNumber;
};

struct upd_spi_speed_cmd
{
  uint32_t busNumber;
  uint64_t frequency;
};

struct upd_spi_mode_cmd
{
  uint32_t busNumber;
  uint32_t mode;
};

struct upd_spi_bits_cmd
{
  uint32_t busNumber;
  uint32_t bits;
};


struct upd_dir_enum_cmd
{
  char* root; // folder to enumerate
  char* result; // data back to app
  uint32_t resultLength; // length of data buffer
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int upd_open(struct file *filep);
static int upd_close(struct file *filep);

static int upd_gpio_interrupt(int irq, void *context, void *arg);

static int upd_handle_pwm(int cmd, unsigned long arg);
static int upd_handle_i2c(int cmd, struct upd_i2c_cmd*);
static struct spi_dev_s * get_spi_bus(int busNumber);
static int upd_handle_spi_data(int cmd, struct upd_spi_data_cmd*);
static int upd_handle_spi_speed(int cmd, struct upd_spi_speed_cmd*);
static int upd_handle_spi_mode(int cmd, struct upd_spi_mode_cmd*);
static int upd_handle_spi_bits(int cmd, struct upd_spi_bits_cmd* data);
static int upd_handle_dir_enum(struct upd_dir_enum_cmd*);
static int upd_config_interrupt(struct upd_gpio_int_config*);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_driver_operations =
{
  .open  = upd_open,
  .close = upd_close,
  .ioctl = upd_ioctl
};

#define QUEUE_NAME          "/mdw_int"
#define QUEUE_MSG_SIZE      16
#define MEADOW_I2C_PORT     1
#define MEADOW_SPI_PORT3    3  // external
#define MEADOW_SPI_PORT2    2  // EXP32


static struct i2c_master_s *g_i2c1 = NULL;
static struct i2c_config_s g_i2c_cfg;

static struct spi_dev_s *g_spi3 = NULL; // external
static struct spi_dev_s *g_spi2 = NULL; // to ESP32

//------- GPIO Interrupt stuff ---------
static mqd_t s_int_queue = 0;
static char queue_buffer[QUEUE_MSG_SIZE];

// the interrupt designator needs to be stored since we pass an address to the interrupt handler
// this array is our "map"
static int s_interruptPinMap[26];

#define MEADOW_UPD_MAX_NUMB_GPIO_W_INTERRUPTS 25

// The following arrays store the informtion in the Id order. The Id
// is what drives the array
    /* ===== MEADOW GPIO PIN MAP =====
    Id  BOARD PIN   SCHEMATIC    CPU PIN = 0x  MDW NAME  ALT FN   IMPLEMENTED?
        J301-1      RESET
        J301-2      3.3
        J301-3      VREF
        J301-4      GND
    00  J301-5      DAC_OUT1        PA4    04     A0
    01  J301-6      DAC_OUT2        PA5    05     A1
    02  J301-7      ADC1_IN3        PA3    06     A2
    03  J301-8      ADC1_IN7        PA7    07     A3
    04  J301-9      ADC1_IN10       PC0    20     A4
    05  J301-10     ADC1_IN11       PC1    21     A5
    06  J301-11     SPI3_CLK        PC10   2A     SCK
    07  J301-12     SPI3_MOSI       PB5    15     MOSI    AF6
    08  J301-13     SPI3_MISO       PC11   2B     MISO    AF6
    09  J301-14     UART4_RX        PI9    89     D00     AF8
    10  J301-15     UART4_TX        PH13   7D     D01     AF8
    11  J301-16     PC6             PC6    26     D02                 *
    12  J301-17     CAN1_RX         PB8    18     D03     AF9
    13  J301-18     CAN1_TX         PB9    19     D04
    14  J302-4      PE3             PE3    43     D15
    15  J302-5      PG3             PG3    63     D14
    16  J302-6      USART1_RX       PB15   1F     D13     AF4
    17  J302-7      USART1_TX       PB14   1E     D12     AF4
    18  J302-8      PC9             PC9    29     D11
    19  J302-9      PH10            PH10   7A     D10
    20  J302-10     PB1             PB1    11     D09
    21  J302-11     I2C1_SCL        PB6    16     D08     AF4
    22  J302-12     I2C1_SDA        PB7    17     D07     AF4
    23  J302-13     PB0             PB0    10     D06
    24  J302-14     PC7             PC7    27
        LED_B       PA0
        LED_G       PA1
        LED_R       PA2
    */
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
  // HistoryBits are shifted from right to left on each tick
  uint32_t HistoryBits;
  // LastReportedState - Last reported to Foundation
  uint8_t LastReportedState;   // Used to determine a "glitch"
  // DebounceState - tracks the current state of each gpio
  uint8_t DebounceState;
  // The number of state bits needed
  uint8_t NeededNumbStateBits;
  // The number of state bits processed for this interrupt
  uint8_t CurrentStateBitsCount;
};

enum debounceStateEnum
{
  dbnc_e_unconfig,    // not configured
  dbnc_e_config,      // Configured, waiting on first interrupt
  dbnc_e_waiting,     // waiting for an interrupt
  dbnc_e_monitoring,  // gpio is being monitored for debounce
};

static struct interruptPinMap_s gpioDebounceData[] =
{
// PinId
  {0x04, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 00 A0
  {0x05, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 01 A1
  {0x06, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 02 A2
  {0x07, STM32_GPIOA_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 03 A3
  {0x20, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 04 A4
  {0x21, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 05 A5
  {0x2A, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 06 SCK
  {0x15, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 07 MOSI
  {0x2B, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 08 MOSO
  {0x89, STM32_GPIOI_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 09 D00
  {0x7D, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 10 D01
  {0x26, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 11 D02
  {0x18, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 12 D03
  {0x19, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 13 D04
  {0x43, STM32_GPIOE_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 14 D05
  {0x63, STM32_GPIOG_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 15 D06
  {0x1F, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 16 D07
  {0x1E, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 17 D08
  {0x29, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 18 D09
  {0x7A, STM32_GPIOH_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 19 D10
  {0x11, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 20 D11
  {0x16, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 21 D12
  {0x17, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 22 D13
  {0x10, STM32_GPIOB_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0},   // 23 D14
  {0x27, STM32_GPIOC_BASE + STM32_GPIO_IDR_OFFSET, 0, 0, 0, 0xff, dbnc_e_unconfig, 0, 0}    // 24 D15
};

// These are optimizations so we don't waste time
static volatile int numbDebounceConfigured = 0;  // incremented and decrementd by config 
// p-m todo static volatile int numbDebounceActive = 0;      // increment by ISR decremented by debouncer

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This is called by Nuttx on every tick (default is every 10 ms)
#if defined CONFIG_SYSTEMTICK_HOOK
void board_timerhook()
{
  // DEBUGGING STUFF
  static uint32_t tickHookCounter = 0;
  tickHookCounter++;
  if(tickHookCounter % (30 * (1000000/CONFIG_USEC_PER_TICK)) == 0)
    syslog(1, "udp-(tick)-30 seconds have passed:%d\n", tickHookCounter);
  // DEBUGGING STUFF

  // Only if at least one gpio configured for interrupt
  if(numbDebounceConfigured == 0)
    return;

  struct interruptPinMap_s *pinDbncAddr;
  for(int offset = 0; offset < MEADOW_UPD_MAX_NUMB_GPIO_W_INTERRUPTS; offset++)
  {
    pinDbncAddr = &gpioDebounceData[offset];

    // Only interested in GPIOs to be monitored
    if(pinDbncAddr->DebounceState != dbnc_e_monitoring)
      continue;

    //syslog(1, "udp-(tick)-0x%02x, Found entry to debounce @ offset:%d\n", pinDbncAddr->PinId, offset);
    
    // For longer delays or faster clocks, we must skip some ticks
    // because our HistoryBits is only 32-bits.
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

    // Make room for current state
    pinDbncAddr->HistoryBits <<= 1;
    if(currentState == 1)
    {
      pinDbncAddr->HistoryBits |= 1;
    }
    pinDbncAddr->CurrentStateBitsCount++;

    // Save the last x bits of the state
    uint32_t lastXBits =  pinDbncAddr->HistoryBits & pinDbncAddr->DebounceMask;

    syslog(1, "udp-(tick)-%02d--0x%02x, state:%s, History prev-0x%08x, new-0x%08x, Mask:0x%08x, lastXBits:0x%08x\n",
            pinDbncAddr->CurrentStateBitsCount,
            pinDbncAddr->PinId,
            currentState == 1 ? "TRUE" : "false",
            oldHistoryBits,
            pinDbncAddr->HistoryBits,
            pinDbncAddr->DebounceMask,
            lastXBits);

    // Have we gotten enough info to determine stable state?
    if(pinDbncAddr->CurrentStateBitsCount < pinDbncAddr->NeededNumbStateBits)
      continue;   // Not yet

    syslog(1, "udp-(tick)-%02d--0x%02x Enough waiting, see if ready\n",
            pinDbncAddr->CurrentStateBitsCount, pinDbncAddr->PinId);

    // If these bits are all 0 or equal to mask we have a stable input state
    if(lastXBits == 0 || lastXBits == pinDbncAddr->DebounceMask)
    {
      // It's been stable long enough
      syslog(1, "udp-(tick)-0x%02x STABLIZED. lastXBits:0x%08x, state: old:%d, new:%d\n",
                pinDbncAddr->PinId, lastXBits, pinDbncAddr->LastReportedState, currentState);

      // Ignore if no state change, it was a glitch.
      if(pinDbncAddr->LastReportedState == currentState)
      {
         syslog(1, "udp-(tick)-0x%02x NO CHANGE. Won't sent state:%d to Foundation\n",
               pinDbncAddr->PinId, currentState);
      }
      else
      {
        syslog(1, "udp-(tick)-0x%02x CHANGED. Will forward to Foundation\n", pinDbncAddr->PinId);

        // Forwared to Foundation
        // queue_buffer[0] = pinDbncAddr->PinId;
        // queue_buffer[1] = currentState;
        // int result = mq_send(s_int_queue, queue_buffer, 2, 0);
        // if(result < 0)
        // {
        //   syslog(LOG_ERR, "mq_send returned:%d\n", result);
        // }
        pinDbncAddr->LastReportedState = currentState;
      }

      syslog(1, "udp-(tick)-0x%02x Done with this input point, Wait for next interrupt\n",
              pinDbncAddr->PinId);
      // Allow ISR to again monitor this GPIO for interrupts
      pinDbncAddr->DebounceState = dbnc_e_waiting;    // Terminate capture
    }
    else
    {
      syslog(1, "udp-(tick)-%02d--0x%02x NOT good enough. Must check more\n",
              pinDbncAddr->CurrentStateBitsCount, pinDbncAddr->PinId);
    }
  }
}
#endif

//===============================================================
static int upd_gpio_interrupt(int irq, void *context, void *arg)
{
  // Only true if at least one gpio configured for interrupt
  if(numbDebounceConfigured == 0)
    return OK;

  struct interruptPinMap_s *pinDbncAddr = (struct interruptPinMap_s *)arg;

  // Properly setup?
  if(pinDbncAddr == NULL)
  {
    syslog(1, "upd-(isr)-- <not setup> pinDbncAddr == NULL [Nothing to do]\n");
    return OK;
  }

  // Configured?
  if(pinDbncAddr->DebounceState == dbnc_e_unconfig)
  {
    syslog(1, "upd-(isr)-- <unconfigured> PinId:0x%02x, pinDbncAddr:%p, DebounceMask:0x%08x\n",
            pinDbncAddr->PinId, pinDbncAddr, pinDbncAddr->DebounceMask);
    return OK;
  }
  
  syslog(1, "upd-(isr)-- <configured!> PinId:0x%02x, pinDbncAddr:%p\n", pinDbncAddr->PinId, pinDbncAddr);

  // Already actively being timed?
  if(pinDbncAddr->DebounceState == dbnc_e_monitoring)
  {
    // Interrupt was caused by switch bounce
    syslog(1, "upd-(isr)--0x%02x <ignore already active>, must be debounce\n", pinDbncAddr->PinId);
    return OK;
  }

  //-----------------------------------------------------
  // Proceed only dbnc_e_config or dbnc_e_waiting are allowed
  uint8_t pinNumb = pinDbncAddr->PinId & 0x0f;
  syslog(1, "upd-(isr)---0x%02x, (P%c%d), pinDbncAddr:%p\n",
          pinDbncAddr->PinId, ((pinDbncAddr->PinId) >> 4) + 'A', pinNumb, pinDbncAddr);

  // IDR Input Data Register - read all 16 for this port
  uint32_t idrRegisterValues = *((uint32_t *)(pinDbncAddr->IDRAddress));
  bool currentState = idrRegisterValues & (1 << pinNumb);
  pinDbncAddr->HistoryBits = currentState ? 1 : 0;

  syslog(1, "upd-(isr)--0x%02x <start timing>, state:%s, HistoryBits:0x%08x, Mask:0x%08x\n",
          pinDbncAddr->PinId, currentState ? "TRUE" : "false",
          pinDbncAddr->HistoryBits, pinDbncAddr->DebounceMask);

  // Only set the debounce timeout period once
  pinDbncAddr->CurrentStateBitsCount = 0;  
  pinDbncAddr->DebounceState = dbnc_e_monitoring;
  return OK;
}

static int upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  struct upd_register_value *register_val;
  struct upd_register_update *register_update;
  struct upd_gpio_int_config *interrupt_cfg;

  switch(cmd)
  {
    case MUPD_SET_REGISTER:
        register_val = (struct upd_register_value *)arg;
        putreg32(register_val->value, register_val->address);
        return OK;
    case MUPD_GET_REGISTER:
        register_val = (struct upd_register_value *)arg;
        register_val->value = getreg32(register_val->address);
        return OK;
    case MUPD_UPDATE_REGISTER:
        // this does an atomic read/set/write of a register
        register_update = (struct upd_register_update *)arg;
        modifyreg32(register_update->address, register_update->clearBits, register_update->setBits);
        return OK;
    case MUPD_REGISTER_GPIO_IRQ:
        interrupt_cfg = (struct upd_gpio_int_config *)arg;
        return upd_config_interrupt(interrupt_cfg);

    case MUPD_PWM_SETUP:
    case MUPD_PWM_SHUTDOWN:
    case MUPD_PWM_START:
    case MUPD_PWM_STOP:
        return upd_handle_pwm(cmd, arg);

    case MUPD_I2C_SHUTDOWN:
    case MUPD_I2C_DATA:
        return upd_handle_i2c(cmd, (struct upd_i2c_cmd*)arg);
      
    case MUPD_SPI_DATA:
        return upd_handle_spi_data(cmd, (struct upd_spi_data_cmd*)arg);
    case MUPD_SPI_SPEED:
        return upd_handle_spi_speed(cmd, (struct upd_spi_speed_cmd*)arg);
    case MUPD_SPI_MODE:
        return upd_handle_spi_mode(cmd, (struct upd_spi_mode_cmd*)arg);
    case MUPD_SPI_BITS:
        return upd_handle_spi_bits(cmd, (struct upd_spi_bits_cmd*)arg);

    case MUPD_DIR_ENUM:
      return upd_handle_dir_enum((struct upd_dir_enum_cmd*)arg);
      break;
    case MUPD_GET_LAST_ERROR:
      *((int*)arg) = errno;
      return OK;

  }
  return ERROR;
}

//===============================================================
static int upd_config_interrupt(struct upd_gpio_int_config* cfg)
{
  struct interruptPinMap_s *pinDbncAddr;
  int ret;
  int i;

  uint32_t designator = cfg->port << 4 | cfg->pin;
  uint32_t pinMapOffset;

  // Walk the gpio data to find the matching entry
  for(i = 0; i < MEADOW_UPD_MAX_NUMB_GPIO_W_INTERRUPTS; i++)
  {
    if(gpioDebounceData[i].PinId == designator)
    {
      pinMapOffset = i;
      break;
    }
  }

  if(i == MEADOW_UPD_MAX_NUMB_GPIO_W_INTERRUPTS)
  {
    // Coding error in gpio data table?
    syslog(1, "upd-(cfg)---BAD NEWS! NO ENTRY FOR %d in gpioDebounceData\n", designator);
    return -1;
  }

  // Save the pointer to the correct entry
  pinDbncAddr = &gpioDebounceData[pinMapOffset];

  syslog(1, "upd-(cfg)---PinId:0x%02x, pinMapOffset:%d, debounceMs:%d, pinDbncAddr:%p\n",
            pinDbncAddr->PinId, pinMapOffset, cfg->debounceMs, pinDbncAddr);

  if(cfg->enable)
  {
    // Cannot control the tick rate so we need to determine if we need to skip ticks
    uint32_t ticksNeeded = ((cfg->debounceMs * 1000)/CONFIG_USEC_PER_TICK) + 1;
    pinDbncAddr->DebounceDivisor = (ticksNeeded / 32) + 1;

    // Create a mask for the number of ticks
    pinDbncAddr->DebounceMask = 0;
    for(i = 0; i <= ticksNeeded / pinDbncAddr->DebounceDivisor; i++)
    {
      pinDbncAddr->DebounceMask <<= 1;
      pinDbncAddr->DebounceMask |= 1;
    }
    pinDbncAddr->NeededNumbStateBits = ticksNeeded;

    syslog(1, "upd-(cfg)---ticksNeeded:%d, DebounceMask:0x%08x, DebounceDivisor:%d\n",
              ticksNeeded,
              pinDbncAddr->DebounceMask,
              pinDbncAddr->DebounceDivisor);

    // Tell Nuttx how to handle interrupts
    ret = stm32_gpiosetevent(
    pinDbncAddr->PinId,
    cfg->risingEdge,
    cfg->fallingEdge,
    0,                    // event
    upd_gpio_interrupt,   // func
    pinDbncAddr);
    
    pinDbncAddr->DebounceState = dbnc_e_config;
    numbDebounceConfigured++;
  }
  else
  {
    numbDebounceConfigured--;    
    pinDbncAddr->DebounceState = dbnc_e_unconfig;

    // Tell Nuttx to forget about interrupts
    ret = stm32_gpiosetevent(
        pinDbncAddr->PinId,
        false, false, 0, NULL, NULL);
  }
  return ret;
}

static int upd_handle_dir_enum(struct upd_dir_enum_cmd* cmd)
{
  DIR *d;
  struct dirent *dir;
  int len = 0;

  d = opendir(cmd->root);
  if(!d) return ENOTDIR;
  while((dir = readdir(d)) != NULL)
  {
    if(len + strlen(dir->d_name) + 1 > cmd->resultLength)
    {
      // this isn't really safe, as the user could always send in fake length data, but for now we assume they are nice users
      break;
    }
    strcat(cmd->result, dir->d_name);
    strcat(cmd->result, "\n");
  }
  closedir(d);
  return OK;
}

static struct spi_dev_s * get_spi_bus(int busNumber)
{
  switch (busNumber)
  {
    case 2:
      if(g_spi2 == NULL)
      {
        g_spi2 = stm32_spibus_initialize(MEADOW_SPI_PORT2);
      }
      return g_spi2;
    case 3:
      if(g_spi3 == NULL)
      {
        g_spi3 = stm32_spibus_initialize(MEADOW_SPI_PORT3);
      }
      return g_spi3;
  }

  return NULL;
}

static int upd_handle_spi_bits(int cmd, struct upd_spi_bits_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETBITS(target, data->bits);

  return OK;
}

static int upd_handle_spi_mode(int cmd, struct upd_spi_mode_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETMODE(target, data->mode);

  return OK;
}

static int upd_handle_spi_speed(int cmd, struct upd_spi_speed_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  SPI_SETFREQUENCY(target, data->frequency);

  return OK;
}

static int upd_handle_spi_data(int cmd, struct upd_spi_data_cmd* data)
{
  struct spi_dev_s *target = get_spi_bus(data->busNumber);

  if(target == NULL)
  {
    return ENODEV;
  }

  // if we have only outbuffer, it's a write
  if(data->txBuffer)
  {
    if(data->rxBuffer)
    {
      // writeread
      SPI_EXCHANGE(target, data->txBuffer, data->rxBuffer, data->length);
    }
    else
    {
      //write
      SPI_SNDBLOCK(target, data->txBuffer, data->length);
    }
  }
  else if(data->rxBuffer > 0)
  {
    // read
    SPI_RECVBLOCK(target, data->rxBuffer, data->length);
  }
  else
  {
    // no read or write buffer
    return EINVAL;
  }
  
  return OK;
}

static int upd_handle_i2c(int cmd, struct upd_i2c_cmd* data)
{
  if(cmd == MUPD_I2C_SHUTDOWN)
  {
    if(g_i2c1 != NULL)
    {
      stm32_i2cbus_uninitialize(g_i2c1);
      g_i2c1 = NULL;
    }
    return OK;
  }

  if(g_i2c1 == NULL)
  {
    // the only I2C port Meadow supports is #1 - just initialize it
    g_i2c1 = stm32_i2cbus_initialize(MEADOW_I2C_PORT);
  }

  g_i2c_cfg.address = data->address;
  g_i2c_cfg.addrlen = 7; // we currently are supporting only 7-bit address devices
  g_i2c_cfg.frequency = data->frequency;

  int result = OK;

  // if we have only outbuffer, it's a write
  if(data->txLength > 0)
  {
    if(data->rxLength > 0)
    {
      // writeread
      result = i2c_writeread(g_i2c1, &g_i2c_cfg, data->txBuffer, data->txLength, data->rxBuffer, data->rxLength);
    }
    else
    {
      //write
      result = i2c_write(g_i2c1, &g_i2c_cfg, data->txBuffer, data->txLength);
    }
  }
  else if(data->rxLength > 0)
  {
    // read
    result = i2c_read(g_i2c1, &g_i2c_cfg, data->rxBuffer, data->rxLength);
  }
  else
  {
    // no read or write buffer
    result = EINVAL;
  }
  
  return result;
}

static int upd_handle_pwm(int cmd, unsigned long arg)
{
  struct upd_pwm_cmd *_upd_pwm_cmd = (struct upd_pwm_cmd *)arg;
  struct pwm_lowerhalf_s *pwm;

  /* Call stm32_pwminitialize() to get an instance of the PWM interface */
  pwm = stm32_pwminitialize(_upd_pwm_cmd->timer);
  if (!pwm)
  {
    aerr("ERROR: Failed to get the STM32 PWM lower half\n");
    return -ENODEV;
  }

  switch(cmd)
  {
  case MUPD_PWM_SETUP:
  {
      pwm->ops->setup(pwm);
      return OK;
  }
  case MUPD_PWM_SHUTDOWN:
  {
      pwm->ops->shutdown(pwm);
      return OK;
  }
  case MUPD_PWM_START:
  {
      struct pwm_info_s info;
      info.frequency = _upd_pwm_cmd->frequency;
#ifdef CONFIG_PWM_MULTICHAN
      pwm->ops->ioctl(pwm, 0, (unsigned long)&info);
      for (int i = 0; i < CONFIG_PWM_NCHANNELS; i++)
      {
        if (info.channels[i].channel == _upd_pwm_cmd->channel)
        {
          info.channels[i].duty = _upd_pwm_cmd->duty;
        }
      }
#else
      info.duty = _upd_pwm_cmd->duty;
#endif

      pwm->ops->start(pwm, &info);
      return OK;
  }
  case MUPD_PWM_STOP:
  {
      struct pwm_info_s info;
#ifdef CONFIG_PWM_MULTICHAN
      pwm->ops->ioctl(pwm, 0, (unsigned long)&info);
      for (int i = 0; i < CONFIG_PWM_NCHANNELS; i++)
      {
        if (info.channels[i].channel == _upd_pwm_cmd->channel)
        {
          info.channels[i].duty = 0;
        }
      }
#else
      info.duty = 0;
#endif

      pwm->ops->start(pwm, &info);
      return OK;
  }
  }

  return OK;
}

static int upd_open(struct file *filep)
{
  struct mq_attr attr;
  attr.mq_flags = 0;
  attr.mq_maxmsg = 10;
  attr.mq_msgsize = QUEUE_MSG_SIZE;
  attr.mq_curmsgs = 0;

  for(int i = 0 ; i < 26 ; i++)
  {
    s_interruptPinMap[i] = 0;
  }

  if(s_int_queue == 0)
  {
    s_int_queue = mq_open(QUEUE_NAME, O_WRONLY | O_CREAT, 0660, &attr);
  }
  return OK;
}

static int upd_close(struct file *filep)
{
  mq_close(s_int_queue);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_gpio_initialize
 *
 * Description:
 *   Initialize GPIO drivers for use with /apps/examples/gpio
 *
 ****************************************************************************/

int meadow_upd_initialize(void)
{
  syslog(LOG_INFO, "+meadow_upd_initialize\n");
  
  // register the driver, passing in our entry points
  int ret = register_driver("/dev/upd", &g_driver_operations, 0666, NULL);
  if (ret)
  {
    return ERROR;
  }

  return OK;
}

