/****************************************************************************
 * configs\stm32f777zit6-meadow\src\hcom_proxy\hcom_proxy_upd.h
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
#ifndef __CONFIGS_MEADOW_SRC_HCOM_PROXY_SHARED__H
#define __CONFIGS_MEADOW_SRC_HCOM_PROXY_SHARED__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

// This header file contains those items that must be shared by apps/hcom
// #include <string.h>

#define HCOM_PROXY_UPD_DRIVER_NAME "/dev/proxyupd"
#define HCOM_PROXY_CMD_HOST_MSG_SIZE 128
#define HCOM_PROXY_CMD_LOG_MSG_SIZE 128

// Battery Backed Register
struct hcom_proxy_upd_bbr_value
{
  uint32_t bbr_id;
  uint32_t value;
};

struct hcom_proxy_upd_bbr_update
{
  uint32_t bbr_id;
  uint32_t clearBits;
  uint32_t setBits;
};

struct hcom_proxy_upd_register_value
{
  uint32_t address;
  uint32_t value;
};

struct hcom_proxy_upd_register_update
{
  uint32_t address;
  uint32_t clearBits;
  uint32_t setBits;
};

struct hcom_proxy_cmd_data
{
  uint32_t proxyCmd;
  uint32_t userData;

  uint8_t logLevel;
  uint8_t logLen;
  uint8_t hostLen;
  uint8_t future;
  char logMsg[HCOM_PROXY_CMD_LOG_MSG_SIZE + 1];
  char hostMsg[HCOM_PROXY_CMD_HOST_MSG_SIZE + 1];
};

// Augment the normal Nuttx LOG_XXXX list
#define LOG_NONE                            0xff

#define HCOM_PROXY_UPD_SET_REGISTER         1
#define HCOM_PROXY_UPD_GET_REGISTER         2
#define HCOM_PROXY_UPD_UPDATE_REGISTER      3
#define HCOM_PROXY_UPD_SET_BBR_VALUE        4
#define HCOM_PROXY_UPD_GET_BBR_VALUE        5
#define HCOM_PROXY_UPD_UPDATE_BBR_VALUE     6
#define HCOM_PROXY_UPD_CLI_COMMAND          7
#define HCOM_PROXY_UPD_GET_MCU_ID           8

//==================================================
#define HCOM_PROXY_CLI_CMD_DEVELOPER_1      1
#define HCOM_PROXY_CLI_CMD_MONO_FLASH       2
#define HCOM_PROXY_CLI_CMD_RESTART_MEADOW   3
#define HCOM_PROXY_CLI_CMD_ERASE_EX_FLASH   4
#define HCOM_PROXY_CLI_CMD_VERIFY_EX_FLASH  5
#define HCOM_PROXY_CLI_CMD_RENEW_FILE_SYS   6

//#define HCOM_PROXY_CLI_CMD_

#endif  // __CONFIGS_MEADOW_SRC_HCOM_PROXY_SHARED__H