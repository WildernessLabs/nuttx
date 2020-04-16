/****************************************************************************
 * configs/stm32f777-zit6-meadow/src/hcom/esp32/hcom_esp32_spi_comms.c
 * 
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
 *   Copyright (C) 2017 Gregory Nutt. All rights reserved.
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

#include "../hcom_common.h"
#include "hcom_esp32_comms.h"

#include <nuttx/board.h>
#include <arch/board/board.h>
#include <nuttx/spi/spi.h>
#include "stm32_spi.h"
#include "stm32_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

// WARNING: This code makes the assumption that the SPI connected to the 
// ESP32 chip is SPI2 
#define HCOM_ESP32_SPI_COMMS_PORT 2

// Pin used to indicate that the ESP32 either has data for the STM32 or it
// is ready to receive data from the STM32.
#define MEADOW_ESP32_SPI_COMMS_READY_PIN_INPUT MEADOW_ESP32_ONBOARD_BOOT_PIN_INPUT

// Pin used to indicate that the ESP32 either has a message waiting to be
// transmitted to the STM32.
#define MEADOW_ESP32_SPI_COMMS_MESSAGE_WAITING_PIN_INPUT (GPIO_INPUT | GPIO_FLOAT | GPIO_SPEED_100MHz | GPIO_PORTI | GPIO_PIN9)

#define MEADOW_ESP32_SPI_COMMS_FREQUENCY 1000000 //04000000UL


/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static bool _shutting_down;
static bool _is_esp32_read_open;
static FAR struct file _esp32_read_fd;
static int _is_esp32_write_open;
static FAR struct file _esp32_write_fd;
static sem_t _initalizeWaitSem;    /* Implements event waiting */
static struct spi_dev_s *_esp_spi_dev;

static uint8_t *esp32_read_buffer;
static bool _esp_spi_initialized;
static bool _init_failed;

static hcom_esp32_spi_comms_callback _esp32_spi_callback;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BUILD_PROTECTED
static int hcom_esp32_spi_comms_kthread(int argc, char *argv[]);
#else
static FAR void *hcom_esp32_spi_comms_pthread(FAR void *arg);
#endif

static int hcom_esp32_spi_comms_make_thread(void);
static int hcom_esp32_spi_comms_gpio_interrupt(int irq, void *context, void *arg);

#define ARRAYSIZE(x) (sizeof((x)) / sizeof((x)[0]))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hcom_esp32_spi_comms_setup()
{
  int ret;

  _shutting_down = false;
  _esp_spi_initialized = false;

  _is_esp32_read_open = false;
  _is_esp32_write_open = false;
  _esp32_spi_callback = 0;

  ret = stm32_configgpio(MEADOW_ESP32_ONBOARD_RESET_PIN_OUTPUT);
  if(ret < 0)
  {
    hcom_utils_f7syslog(LOG_CRIT, "%s@%d-Config GPIO failed ret:%d\n", thisFile, __LINE__, ret);
    return -1;
  }

  ret = stm32_configgpio(MEADOW_ESP32_SPI_COMMS_READY_PIN_INPUT);
  if(ret < 0)
  {
    hcom_utils_f7syslog(LOG_CRIT, "%s@%d-Config GPIO failed ret:%d\n", thisFile, __LINE__, ret);
    return -1;
  }

  stm32_gpiosetevent(MEADOW_ESP32_SPI_COMMS_READY_PIN_INPUT,
     /*risingedge=*/true, /*fallingedge=*/ true, true,
        hcom_esp32_spi_comms_gpio_interrupt, 0);

  _esp_spi_dev = stm32_spibus_initialize(HCOM_ESP32_SPI_COMMS_PORT);
  if (_esp_spi_dev == 0)
  {
    hcom_utils_f7syslog(LOG_CRIT, "%s@%d-Error:Failed init esp32 SPI port %d\n", thisFile, __LINE__, ret);
    return ERROR;
  }

  SPI_SETFREQUENCY(_esp_spi_dev, MEADOW_ESP32_SPI_COMMS_FREQUENCY);
  SPI_SETBITS(_esp_spi_dev, 8);
  SPI_SETMODE(_esp_spi_dev, SPIDEV_MODE3); /* CPOL=1 CHPHA=1 */

#if 0
  int status = SPI_STATUS(_esp_spi_dev, _esp_spi_dev_id);
#endif

  hcom_esp32_spi_comms_read_loop();

  return OK;
}

//====================================================================
void hcom_esp32_spi_comms_shutdown(void)
{
  _shutting_down = true;
}

//===================================================================================
// Wait for the thread holding semaphore to release it
static void hcom_esp32_spi_takesem(void)
{
  int ret;
  do
  {
    ret = sem_wait(&_initalizeWaitSem);    // Take the semaphore (perhaps waiting)
    // The only case that an error should occur here is if the wait was awakened by a signal
    DEBUGASSERT(ret == OK || ret == -EINTR);
  }
  while (ret == -EINTR);
}

//=================================================================
// This thread receives all SPI messages received from ESP32
#ifdef CONFIG_BUILD_PROTECTED
int hcom_esp32_spi_comms_kthread(int argc, char *argv[])
#else
FAR void *hcom_esp32_spi_comms_pthread(FAR void *arg)
#endif
{
  //int ret;
  
#if HCOM_TASK_SHOW_CREATED_TASK_INFORMATION > 0
  struct tcb_s *rtcb = this_task();
  syslog(0, "Created Task:'%s' as #%d\n", rtcb->name, getpid());
#endif

#if 0
  ret = hcom_esp32_spi_phase2_initialization();
  if(ret < 0)
  {
    f7syslog(LOG_ERR, "%s@%d-Error:phase 2 initialization failed\n", thisFile, __LINE__);
    // Error noted
    _init_failed = ret;
    sem_post(&_initalizeWaitSem);  // Allow hcom thread to proceed
    return ret; //  This will kill this thread
  }

  // Thread created and initialization successful
  sem_post(&_initalizeWaitSem);  // Allow hcom thread to proceed

  // Allow hcom thread to start before using this thread
  usleep(500 * 1000);

  while(!_shutting_down)
  {
    ret = hcom_esp32_spi_comms_read_loop();
    if(ret < 0)
    {
      sleep(1);   // prevent tight loops
    }
  }
#endif

#ifdef CONFIG_BUILD_PROTECTED
  return OK;
#else
  return NULL;    // Keeps compiler happy
#endif
}

//====================================================================
// Read from the ESP32 SPI port
int hcom_esp32_spi_comms_read_loop(void)
{
  int _esp_spi_dev_id = SPIDEV_USER(0);

  while(1)
  {
    uint8_t data_tx[256];
    uint8_t data_rx[256];
    memset(data_tx, 0, ARRAYSIZE(data_tx));
    memset(data_rx, 0, ARRAYSIZE(data_rx));
    strcpy((char*) data_tx, "Hello from STM32");

    SPI_SELECT(_esp_spi_dev, _esp_spi_dev_id, /*selected=*/true);
    SPI_EXCHANGE(_esp_spi_dev, data_tx, data_rx, ARRAYSIZE(data_tx) / 8);
    SPI_SELECT(_esp_spi_dev, _esp_spi_dev_id, /*selected=*/false);
  }

  return OK;
}

//====================================================================
// Process the GPIO interrupt associated with ESP32 SPI comms
int hcom_esp32_spi_comms_gpio_interrupt(int irq, void *context, void *arg)
{
  // Callback into user code callback.
  if (_esp32_spi_callback != 0)
    _esp32_spi_callback(irq, context);
}

//====================================================================
// Associates a callback for signaling user code with ESP32 SPI comms signals
void hcom_esp32_spi_comms_set_callback(hcom_esp32_spi_comms_callback cb)
{
  _esp32_spi_callback = cb;
}


