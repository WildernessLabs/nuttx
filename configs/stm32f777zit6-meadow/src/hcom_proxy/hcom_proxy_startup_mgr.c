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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "hcom_proxy_common.h"

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_proxy_setup_mgr(FAR struct mtd_dev_s *mtd)
{
  int ret;

  if (mtd == NULL)
  {
    return ERROR;
  }
  
  ret = hcom_proxy_upd_initialize();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup hcom proxy upd:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  ret = hcom_utils_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup hcom utils:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  // Sets a few internal variable states
  ret = hcom_exec_ex_flash_setup(mtd);
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup misc %d\n", thisFile, __LINE__, ret);
    return ret;
  }

//   // Sets the MTD for the file system
//   // Note: the calling thread is the nuttx startup thread. Any activity here may delay the
//   //  remainder of nuttx from starting, which may be determined to be a good thing.
//   ret = hcom_exec_flash_fs_setup(mtd);
//   if (ret < 0)
//   {
//     syslog(LOG_CRIT, "%s@%d-setup F/S %d\n", thisFile, __LINE__, ret);
//     return ret;
//   }

//   // Sets a few internal variable states
//   ret = hcom_file_commands_setup();
//   if (ret < 0)
//   {
//     syslog(LOG_CRIT, "%s@%d-setup file cmds %d\n", thisFile, __LINE__, ret);
//     return ret;
//   }

  ret = hcom_fs_setup(mtd);
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup F/S helper %d\n", thisFile, __LINE__, ret);
    return ret;
  }

// #if defined(CONFIG_HCOM_MTD_STRESS_TEST)
//   ret = hcom_exec_rqst_testing_setup(mtd);
//   if (ret < 0)
//   {
//     syslog(LOG_CRIT, "%s@%d-setup testing %d\n", thisFile, __LINE__, ret);
//     return ret;
//   }
// #endif

// Initialize the file system as needed
#if defined(CONFIG_HCOM_FILESYSTEM_INIT)    // defined in menuconfig
  ret = hcom_fs_init_file_system();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup F/S helper %d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

  return OK;
}
