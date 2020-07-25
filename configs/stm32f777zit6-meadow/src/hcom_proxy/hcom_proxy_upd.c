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
// #include <nuttx/mqueue.h>
// #include <nuttx/signal.h>
// #include <nuttx/drivers/pwm.h>
// #include <nuttx/spi/spi.h>

#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "chip.h"
#include "fcntl.h"
// #include "stm32_pwm.h"
// #include "stm32_i2c.h"
// #include "stm32f777zit6-meadow.h"
// #include "stm32_spi.h"

#include <dirent.h>

// #include <nuttx/timers/timer.h>
#include <sys/ioctl.h>
// #include "stm32_tim.h"
#include "hcom_proxy_upd.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcp_upd_open(struct file *filep);
static int hcp_upd_close(struct file *filep);
static int hcp_upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int hcp_upd_read(FAR struct file *filep, FAR char *buffer, size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_hcom_proxy_operations =
{
  .open  = hcp_upd_open,
  .close = hcp_upd_close,
  .ioctl = hcp_upd_ioctl,
  .read  = hcp_upd_read
};

// Called when hcom proxy upd driver is opened
int hcp_upd_open(struct file *filep)
{
  return OK;
}

// Called when hcom proxy upd driver is closed
int hcp_upd_close(struct file *filep)
{
  return OK;
}

// Added read because open failed even with O_RDONLY which is 0
int hcp_upd_read(FAR struct file *filep, FAR char *buffer, size_t buflen)
{
  return OK;
}

static int hcp_upd_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  struct hcom_proxy_upd_register_value *register_val;
  struct hcom_proxy_upd_register_update *register_update;
  struct hcom_proxy_upd_first_test *first_test;

  switch(cmd)
  {
    syslog(1, "Entered %s()\n", __func__);  usleep(50 * 1000);
    
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
    case HCOM_PROXY_UPD_FIRST_TEST:
      first_test = (struct hcom_proxy_upd_first_test *)arg;
      syslog(1, "Have Reached HPUPD_FIRST_TEST! value:%d, PID:%d\n", first_test->value, getpid());  usleep(50 * 1000);
      return OK;
  }
  return ERROR;
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

