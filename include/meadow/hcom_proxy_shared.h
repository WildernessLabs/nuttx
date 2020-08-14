/****************************************************************************
 * ..\Meadow\Meadow.OS\nuttx\include\meadow\hcom_proxy_shared.h
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
#ifndef __INCLUDE_MEADOW_HCOM_PROXY_SHARED__H
#define __INCLUDE_MEADOW_HCOM_PROXY_SHARED__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Private defines
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

struct hcom_proxy_upd_is_part_mounted
{
  uint32_t partitionId;
  bool isMounted;
};

struct hcom_proxy_cmd_data
{
  uint16_t hcomCmd;
  uint32_t proxyCmd;
  uint32_t userData;

  uint8_t logLevel;
  uint8_t logLen;
  char logMsg[HCOM_PROXY_CMD_LOG_MSG_SIZE + 1];
  void (* send_host_msg)(uint16_t, uint32_t, char *, char *, int);
};

struct hcom_proxy_upd_gpio_config_s
{
  uint8_t gpioDefn;     // 1- n
  uint8_t configValue;  // 1 = input, 0 = output
  int result;
};

struct hcom_proxy_upd_gpio_write_s
{
  uint8_t gpioDefn;   // 1- n
  uint8_t cmdValue;   // 1 = high, 0 = low
};

struct hcom_proxy_upd_gpio_output_map_s
{
  uint32_t gpio_output_defn;
};

struct hcom_proxy_upd_gpio_input_map_s
{
  uint32_t gpio_input_defn;
};

//==================================================
// hcom proxy upd ioctl commands
#define HCOM_PROXY_UPD_SET_REGISTER         1
#define HCOM_PROXY_UPD_GET_REGISTER         2
#define HCOM_PROXY_UPD_UPDATE_REGISTER      3
#define HCOM_PROXY_UPD_SET_BBR_VALUE        4
#define HCOM_PROXY_UPD_GET_BBR_VALUE        5
#define HCOM_PROXY_UPD_UPDATE_BBR_VALUE     6
#define HCOM_PROXY_UPD_CLI_COMMAND          7
#define HCOM_PROXY_UPD_GET_MCU_ID           8
#define HCOM_PROXY_UPD_IS_PART_MOUNTED      9
#define HCOM_PROXY_UPD_GPIO_COMMAND         10

// Augments the normal Nuttx LOG_XXXX list
#define LOG_NONE                            0xff
#define HCOM_PROXY_UPD_GPIO_CONFIG          11

// These define GPIOs that cannot be access from
// the apps side
#define HCOM_PROXY_GPIO_CONFIG_INPUT        1
#define HCOM_PROXY_GPIO_CONFIG_OUTPUT       0
#define HCOM_PROXY_GPIO_CMD_VALUE_HIGH      1
#define HCOM_PROXY_GPIO_CMD_VALUE_LOW       0

// The order here is imporatant because these are used as offsets
// into hcom_proxy_udp
enum HcomProxyGpioDefn
{
  HcomProxyGpioDefnEspReset,
  HcomProxyGpioDefnEspBoot,
  HcomProxyGpioDefnA0___01,
  HcomProxyGpioDefnA1___02,
  HcomProxyGpioDefnA2___03,
  HcomProxyGpioDefnA3___04,
  HcomProxyGpioDefnA4___05,
  HcomProxyGpioDefnA5___06,
  HcomProxyGpioDefnSCK__07,
  HcomProxyGpioDefnMOSI_08,
  HcomProxyGpioDefnMISO_09,
};

#endif  // __INCLUDE_MEADOW_HCOM_PROXY_SHARED__H