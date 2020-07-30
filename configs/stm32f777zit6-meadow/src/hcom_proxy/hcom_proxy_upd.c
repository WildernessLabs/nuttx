/****************************************************************************
 * configs\stm32f777zit6-meadow\src\hcom_proxy_upd.c
 * 
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
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

// This file was inspired by (and copied from) Chris Tacke's
// 'Universal Platform Driver' (Meadow-upd.c). Thanks Chris!

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>

#include <nuttx/config.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <arch/board/board.h>

#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "chip.h"
#include "fcntl.h"

#include <dirent.h>
#include <sys/ioctl.h>

#include "hcom_proxy_shared.h"
#include "hcom_proxy_common.h"

#include "chip/stm32f76xx77xx_memorymap.h"
#include "chip/stm32_rtcc.h"    // battery backed registers and ram
#include "stm32_uid.h"          // stm32_get_uniqueid()

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_upd_proxy_open(struct file *filep);
static int hcom_upd_proxy_close(struct file *filep);
static int hcom_upd_proxy_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
// Added read because open failed without it. The open flag O_RDONLY == 0.
static int hcom_upd_proxy_read(FAR struct file *filep, FAR char *buffer, size_t buflen);
static uint32_t hcom_proxy_upd_find_bbr_addr(uint32_t bbrId);

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static const struct file_operations g_hcom_proxy_operations =
{
  .open  = hcom_upd_proxy_open,
  .close = hcom_upd_proxy_close,
  .read  = hcom_upd_proxy_read,
  .ioctl = hcom_upd_proxy_ioctl
};

// ====================================================================
// Called when hcom proxy upd driver is opened
int hcom_upd_proxy_open(struct file *filep)
{
  return OK;
}

// ====================================================================
// Called when hcom proxy upd driver is closed
int hcom_upd_proxy_close(struct file *filep)
{
  return OK;
}

// ====================================================================
int hcom_upd_proxy_read(FAR struct file *filep, FAR char *buffer, size_t buflen)
{
  return OK;
}

// ====================================================================
static int hcom_upd_proxy_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  uint32_t ret;
  uint32_t tempData;
  struct hcom_proxy_upd_register_value *register_val;
  struct hcom_proxy_upd_register_update *register_update;
  struct hcom_proxy_upd_bbr_value *bbr_val;
  struct hcom_proxy_upd_bbr_update *bbr_update;
  struct hcom_proxy_cmd_data *cmdData;

  switch(cmd)
  {    
    // This work with any register
    case HCOM_PROXY_UPD_SET_REGISTER:
      register_val = (struct hcom_proxy_upd_register_value *)arg;
      putreg32(register_val->value, register_val->address);
      return OK;
    case HCOM_PROXY_UPD_GET_REGISTER:
      register_val = (struct hcom_proxy_upd_register_value *)arg;
      register_val->value = getreg32(register_val->address);
      return OK;
    case HCOM_PROXY_UPD_UPDATE_REGISTER:
        // this does an atomic read/set/write of a register
      register_update = (struct hcom_proxy_upd_register_update *)arg;
      modifyreg32(register_update->address, register_update->clearBits, register_update->setBits);
      return OK;

      // This only work with Battery Backed Registers via 0-31 id value
    case HCOM_PROXY_UPD_SET_BBR_VALUE:
      bbr_val = (struct hcom_proxy_upd_bbr_value *)arg;
      putreg32(bbr_val->value, hcom_proxy_upd_find_bbr_addr(bbr_val->bbr_id));
      return OK;
    case HCOM_PROXY_UPD_GET_BBR_VALUE:
      bbr_val = (struct hcom_proxy_upd_bbr_value *)arg;
      bbr_val->value = getreg32(hcom_proxy_upd_find_bbr_addr(bbr_val->bbr_id));
      return OK;
    case HCOM_PROXY_UPD_UPDATE_BBR_VALUE:
      bbr_update = (struct hcom_proxy_upd_bbr_update *)arg;
      tempData = hcom_proxy_upd_find_bbr_addr(bbr_update->bbr_id);
      modifyreg32(tempData, bbr_update->clearBits, bbr_update->setBits);
      return OK;

    case HCOM_PROXY_UPD_CLI_COMMAND:
      cmdData = (struct hcom_proxy_cmd_data *)arg;
      ret = hcom_proxy_route_cli_command(cmdData);
      return ret;

    case HCOM_PROXY_UPD_GET_MCU_ID:
      stm32_get_uniqueid((uint8_t *)arg);
      return OK;

    default:
      syslog(LOG_ERR, "%s@%d-unknown hcom proxy upd command:%d\n", thisFile, __LINE__, cmd);

  }

  return ERROR;
}

// ====================================================================
// Map battery backed register
// (at this time only #31 is used)
uint32_t hcom_proxy_upd_find_bbr_addr(uint32_t bbrId)
{
  uint32_t bbrAddr;

  // A value 0 - 31 relates to Battery Backed Registers 1 - 31
  DEBUGASSERT(bbrId >= 0 && bbrId < 32);
  switch(bbrId)
  {
    case 0: bbrAddr = STM32_RTC_BK0R; break;
    case 1: bbrAddr = STM32_RTC_BK1R; break;
    case 2: bbrAddr = STM32_RTC_BK2R; break;
    case 3: bbrAddr = STM32_RTC_BK3R; break;
    case 4: bbrAddr = STM32_RTC_BK4R; break;
    case 5: bbrAddr = STM32_RTC_BK5R; break;
    case 6: bbrAddr = STM32_RTC_BK6R; break;
    case 7: bbrAddr = STM32_RTC_BK7R; break;
    case 8: bbrAddr = STM32_RTC_BK8R; break;
    case 9: bbrAddr = STM32_RTC_BK9R; break;
    case 10: bbrAddr = STM32_RTC_BK10R; break;
    case 11: bbrAddr = STM32_RTC_BK11R; break;
    case 12: bbrAddr = STM32_RTC_BK12R; break;
    case 13: bbrAddr = STM32_RTC_BK13R; break;
    case 14: bbrAddr = STM32_RTC_BK14R; break;
    case 15: bbrAddr = STM32_RTC_BK15R; break;
    case 16: bbrAddr = STM32_RTC_BK16R; break;
    case 17: bbrAddr = STM32_RTC_BK17R; break;
    case 18: bbrAddr = STM32_RTC_BK18R; break;
    case 19: bbrAddr = STM32_RTC_BK19R; break;
    case 20: bbrAddr = STM32_RTC_BK20R; break;
    case 21: bbrAddr = STM32_RTC_BK21R; break;
    case 22: bbrAddr = STM32_RTC_BK22R; break;
    case 23: bbrAddr = STM32_RTC_BK23R; break;
    case 24: bbrAddr = STM32_RTC_BK24R; break;
    case 25: bbrAddr = STM32_RTC_BK25R; break;
    case 26: bbrAddr = STM32_RTC_BK26R; break;
    case 27: bbrAddr = STM32_RTC_BK27R; break;
    case 28: bbrAddr = STM32_RTC_BK28R; break;
    case 29: bbrAddr = STM32_RTC_BK29R; break;
    case 30: bbrAddr = STM32_RTC_BK30R; break;
    case 31: bbrAddr = STM32_RTC_BK31R; break;
    default:
    syslog(LOG_ERR, "%s@%d-unknown hcom proxy bbr id:%d\n", thisFile, __LINE__, bbrId);
  }
  return bbrAddr;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_proxy_upd_initialize
 *
 * Description:
 *   Initialize hcom proxy upd
 *
 ****************************************************************************/

int hcom_proxy_upd_initialize(void)
{
  syslog(LOG_INFO, "+hcom_proxy_upd_initialize\n");
  
  // register the driver, passing in our entry points
  int ret = register_driver(HCOM_PROXY_UPD_DRIVER_NAME, &g_hcom_proxy_operations, 0666, NULL);
  if (ret)
  {
    return ERROR;
  }

  return OK;
}

