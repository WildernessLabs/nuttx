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
#ifndef __CONFIGS_MEADOW_SRC_HCOM_PROXY_UPD__H
#define __CONFIGS_MEADOW_SRC_HCOM_PROXY_UPD__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

// #include <string.h>

#define HCOM_PROXY_UPD_DRIVER_NAME "/dev/hcomproxyupd"

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

struct hcom_proxy_upd_first_test
{
  uint32_t value;
};

#define HCOM_PROXY_UPD_SET_REGISTER    1
#define HCOM_PROXY_UPD_GET_REGISTER    2
#define HCOM_PROXY_UPD_UPDATE_REGISTER 3
#define HCOM_PROXY_UPD_FIRST_TEST      4

#endif  // __CONFIGS_MEADOW_SRC_HCOM_PROXY_UPD__H