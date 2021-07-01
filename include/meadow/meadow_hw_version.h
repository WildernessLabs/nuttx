/****************************************************************************
 * nuttx\include\meadow\meadow_hw_version.h
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

#ifndef __CONFIGS_MEADOW_SRC_MEADOW_HW_VERSION__H
#define __CONFIGS_MEADOW_SRC_MEADOW_HW_VERSION__H

#include <nuttx/config.h>

#include <sys/types.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/spi/qspi.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// Standard qspi flash read id command
#define MEADOW_QSPI_FLASH_READ_ID_COMMAND (0x9f)

// Define each Meadow version
#define MEADOW_MICRO_VERSION_UNKNOWN (0)
#define MEADOW_MICRO_VERSION_F7v1 (1)
#define MEADOW_MICRO_VERSION_F7v2 (2)

// Size of each versions flash
#define MEADOW_MICRO_VERSION_F7v2_FLASH_SIZE (67108864)   // 64 MB
#define MEADOW_MICRO_VERSION_F7v1_FLASH_SIZE (33554432)   // 32 MB

//-------------------------------------------------------------------
// The following define the Manufacture, type and capacity of the flash
// memory chips used on the following models of the Meadow F7 Micro.
// F7v1 uses a spansion flash
// Spansion id 0x01, type 0x60, capacity 19 (256 bytes)
#define MEADOW_QSPI_FLASH_SPANSION_S25FL256L    (0x00016019)

// F7v2 uses a winbond 512 flash
// Winbond id 0xef, type 0x40 (Q) or 0x70 (M), capacity 20 (512 bytes)
#define MEADOW_QSPI_FLASH_WINBOND_W25Q512JVxxQ  (0x00EF4020) // 'Q' version, default QE = 1
#define MEADOW_QSPI_FLASH_WINBOND_W25Q512JVxxM  (0x00EF7020) // 'M' version, default QE = 0

//-------------------------------------------------------------------
// The following are duplicates of what's available within each driver
// They are here because previously they were hardcoded values in
// configs\stm32f777zit6-meadow\src\stm32_appinitialize.c and this is
// more obvious.
// If the driver values change then obviously the following must change too
#if defined(CONFIG_MTD_S25FL)
  // keep in sync with s25fl.c driver
  #define MTD_S25FL_FLASH_QSPI_ADDRLEN       (4)
  #define MTD_S25FL_FLASH_READ_QUADIO        (0xeb)
  #define MTD_S25FL_FLASH_NUMBER_DUMMIES     (10)
#endif
#if defined(CONFIG_MTD_W25QXXXJV)
  // Keep in sync with w25qxxxjv.c driver
  #define MTD_W25QJV_FLASH_QSPI_ADDRLEN       (4)   // W25QxxxJV, xxx < 256 ADDRLEN=3, for xxx >= 256 ADDRLEN=4
  #define MTD_W25QJV_FLASH_READ_QUADIO        (0xeb)
  #define MTD_W25QJV_FLASH_NUMBER_DUMMIES     (6)   // This needs to match the W25QxxxJV driver
#endif

// Public functions
uint32_t meadow_hw_version_determine(FAR struct qspi_dev_s *qspi);
uint32_t meadow_hw_version_return(void);

#endif // __CONFIGS_MEADOW_SRC_MEADOW_HW_VERSION__H