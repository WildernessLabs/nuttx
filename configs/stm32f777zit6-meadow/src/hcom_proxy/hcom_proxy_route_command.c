/****************************************************************************
 * configs\stm32f777zit6-meadow\src\hcom_proxy\hcom_proxy_startup_mgr.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
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

// This is where CLI commands are handled

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "hcom_proxy_common.h"
#include "hcom_proxy_shared.h"

#include <assert.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
// Proxy playground
int hcom_developer_1(struct hcom_proxy_cmd_data *cmdData)
{
  cmdData->logLevel = LOG_WARNING;
  cmdData->logLen = snprintf(cmdData->logMsg, HCOM_PROXY_CMD_LOG_MSG_SIZE,
            "%s@%d-Developer1 is not a real command:0x%08x.\n", thisFile, __LINE__, cmdData->proxyCmd);

  char hostMsg[HCOM_PROXY_CMD_HOST_MSG_SIZE];
  snprintf(hostMsg, HCOM_PROXY_CMD_HOST_MSG_SIZE,
            "%s@%d-'Developer1 received userData:%d and command:0x%08x'.",
            thisFile, __LINE__, cmdData->userData, cmdData->proxyCmd);
  cmdData->send_host_text(hostMsg, 0, thisFile, __LINE__);
  return -1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hcom_proxy_route_cli_command(struct hcom_proxy_cmd_data *cmdData)
{
  int ret;

  switch(cmdData->proxyCmd)
  {
    case HCOM_PROXY_CLI_CMD_DEVELOPER_1:
      ret = hcom_developer_1(cmdData);
      return ret;

    case HCOM_PROXY_CLI_CMD_RESTART_MEADOW:
      up_systemreset();
      return OK;

    case HCOM_PROXY_CLI_CMD_MONO_FLASH:
      ret = hcom_exec_ex_flash_mono_flash(cmdData);
      return ret;
  
    case HCOM_PROXY_CLI_CMD_ERASE_EX_FLASH:
      ret = hcom_exec_ex_flash_erase_ex_flash(cmdData);
      return ret;

    case HCOM_PROXY_CLI_CMD_VERIFY_EX_FLASH:
      ret = hcom_exec_ex_flash_verify_ex_flash(cmdData);
      return ret;

    case HCOM_PROXY_CLI_CMD_RENEW_FILE_SYS:
      // p-m implement
      ret = OK;
      return ret;

    default:
      cmdData->logLevel = LOG_ERR;
      cmdData->logLen = snprintf(cmdData->logMsg, HCOM_PROXY_CMD_LOG_MSG_SIZE,
              "%s@%d-Undefined command:0x%08x.\n", thisFile, __LINE__, cmdData->proxyCmd);
  }
  return -1;
}


