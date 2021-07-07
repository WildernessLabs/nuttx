/****************************************************************************
 * \configs\stm32f777zit6-meadow\src\hcom_nx\meadow_hw_version.c
 * 
 *   Copyright (C) 2021 Wilderness Labs. All rights reserved.
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

// This module exists to allow the version of Meadow to be determined at runtime.
// This may include a number of different tests.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <meadow/meadow_hw_version.h>
#include <nuttx/spi/qspi.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

/************************************************************************************
 * Private Data
 ************************************************************************************/

static uint32_t _meadowVer;
static bool _meadowVersionKnown = false;

/************************************************************************************
 * Public Data
 ************************************************************************************/

// Each row represents one Meadow GPIO available to the user. This table
// provides the gpio pin differences for the various Meadow Micro F7
// hardware versions.
// The GPIOs in this table are primarily for interrupt support.
meadow_hw_ver_gpio_pin_defns_t meadow_hw_ver_gpio_pins[] =
{
/*   Name   v1    v2     v1Pin  v2Pin v2 Gpio base Addr
/* 00 A0   PA4   PA4 */  {0x04, 0x04, STM32_GPIOA_IDR },
/* 01 A1   PA5   PA5 */  {0x05, 0x05, STM32_GPIOA_IDR },
/* 02 A2   PA3   PA3 */  {0x03, 0x03, STM32_GPIOA_IDR },
/* 03 A3   PA7   PB0 */  {0x07, 0x10, STM32_GPIOB_IDR },
/* 04 A4   PC0   PB1 */  {0x20, 0x11, STM32_GPIOB_IDR },
/* 05 A5   PC1   PC0 */  {0x21, 0x20, STM32_GPIOC_IDR },
/* 06 SCK  PC10  PC10*/  {0x2A, 0x2A, STM32_GPIOC_IDR },
/* 07 COPI PB5   PB5 */  {0x15, 0x15, STM32_GPIOB_IDR },
/* 08 COPO PC11  PC11*/  {0x2B, 0x2B, STM32_GPIOC_IDR },
/* 09 D00  PI9   PI9 */  {0x89, 0x89, STM32_GPIOI_IDR },
/* 10 D01  PH13  PH13*/  {0x7D, 0x7D, STM32_GPIOH_IDR },
/* 11 D02  PC6   PH10*/  {0x26, 0x7A, STM32_GPIOH_IDR },
/* 12 D03  PB8   PB8 */  {0x18, 0x18, STM32_GPIOB_IDR },
/* 13 D04  PB9   PB9 */  {0x19, 0x19, STM32_GPIOB_IDR },
/* 14 D05  PC7   PB4 */  {0x27, 0x14, STM32_GPIOB_IDR },
/* 15 D06  PB0   PB13*/  {0x10, 0x1D, STM32_GPIOB_IDR },
/* 16 D07  PB7   PB7 */  {0x17, 0x17, STM32_GPIOB_IDR },
/* 17 D08  PB6   PB6 */  {0x16, 0x16, STM32_GPIOB_IDR },
/* 18 D09  PB1   PC6 */  {0x11, 0x26, STM32_GPIOC_IDR },
/* 19 D10  PH10  PC7*/   {0x7A, 0x27, STM32_GPIOC_IDR },
/* 20 D11  PC9   PC9 */  {0x29, 0x29, STM32_GPIOC_IDR },
/* 21 D12  PB14  PB14*/  {0x1E, 0x1E, STM32_GPIOB_IDR },
/* 22 D13  PB15  PB15*/  {0x1F, 0x1F, STM32_GPIOB_IDR },
/* 23 D14  PG3   PB12*/  {0x63, 0x2C, STM32_GPIOB_IDR },
/* 24 D15  PE3   PG12*/  {0x43, 0x6C, STM32_GPIOG_IDR }
};

#define MEADOW_HW_VER_GPIO_PIN_DEFNS_SIZE \
  (sizeof(meadow_hw_ver_gpio_pins) / sizeof(meadow_hw_ver_gpio_pin_defns_t))

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

static int meadow_read_qspi_hw_version(FAR struct qspi_dev_s *qspi, uint8_t cmd,
                                  FAR void *buffer, size_t buflen);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Just return the already found hardware version information
uint32_t meadow_hw_version_return(void)
{
  if(_meadowVersionKnown)
    return _meadowVer;
  
  return MEADOW_MICRO_VERSION_UNKNOWN;
}

//==================================================================
// This function will query the meadow's flash chip and read it's 3 byte ID
// and return the Meadow version
uint32_t meadow_hw_version_determine(FAR struct qspi_dev_s *qspi)
{
  int ret;
  uint32_t flashId;
  uint8_t devInfo[3];

  // We only want to do use the QSPI interface at startup. During
  // initialization phase the QSPI communications parameters are
  // changed to more specific values that will only work for the
  // board specific flash chip. For this phase the parameters are
  // general and generic.
  if(_meadowVersionKnown)
    return _meadowVer;
    
  ret = meadow_read_qspi_hw_version(qspi, MEADOW_QSPI_FLASH_READ_ID_COMMAND, devInfo, 3);
  if(ret < 0)
  {
    syslog(LOG_ERR, "Initial QSPI read failed:%d\n", ret);
    return ret;
  }

  flashId = (uint32_t) (devInfo[0] << 16 | devInfo[1] << 8 | devInfo[2]);

  switch(flashId)
  {
    case MEADOW_QSPI_FLASH_SPANSION_S25FL256L:
    _meadowVer = MEADOW_MICRO_VERSION_F7v1;
    break;

    case MEADOW_QSPI_FLASH_WINBOND_W25Q512JVxxQ:
    case MEADOW_QSPI_FLASH_WINBOND_W25Q512JVxxM:
    _meadowVer = MEADOW_MICRO_VERSION_F7v2;
    break;

    default:
    _meadowVer = MEADOW_MICRO_VERSION_UNKNOWN;
  }

  syslog(LOG_INFO, "Meadow hardware version:%d, Flash chip mfg:0x%02x chip type:0x%02x, capacity:0x%02x\n",
        _meadowVer, devInfo[0], devInfo[1], devInfo[2]);

  _meadowVersionKnown = true;
  return _meadowVer;
}

//============================================================================
// Read bytes from the QSPI flash
int meadow_read_qspi_hw_version(FAR struct qspi_dev_s *qspi, uint8_t cmd,
                                  FAR void *buffer, size_t buflen)
{
  struct qspi_cmdinfo_s cmdinfo;

  // Lock the bus
  (void)QSPI_LOCK(qspi, true);

  QSPI_SETMODE(qspi, QSPIDEV_MODE3);
  QSPI_SETBITS(qspi, 8);

  // Pick a somewhat slow speed for this single read. After the appropriate
  // driver is initialized, the driver will pick the best speed for the chip
  (void)QSPI_SETFREQUENCY(qspi, 48000000);

  cmdinfo.flags   = QSPICMD_READDATA;
  cmdinfo.addrlen = 0;
  cmdinfo.cmd     = cmd;
  cmdinfo.buflen  = buflen;
  cmdinfo.addr    = 0;
  cmdinfo.buffer  = buffer;

  // Send the read command
  int ret = QSPI_COMMAND(qspi, &cmdinfo);
  
  // Unlock
  (void)QSPI_LOCK(qspi, false);

  return ret;
}

//============================================================================
char *meadow_hw_version_string_return(void)
{
  char *hwReturn;
  uint32_t hwVersion = meadow_hw_version_return();

  static char *verName[] = 
  {
    "Unknown",
    "F7v1",
    "F7v2"
  };

  return (hwVersion < 1 || hwVersion > 2) ? verName[0] : verName[hwVersion];
}
