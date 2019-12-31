/****************************************************************************
 * drivers/syslog/syslog_semihosting.c
 *
 *   Copyright (C) 2019 Wilderness Labs. All rights reserved.
 *   Author: Joao Matos <joao@tritao.eu>
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

#include <nuttx/config.h>

#include <stdlib.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/syslog/syslog.h>

#include "syslog.h"

#ifdef CONFIG_SYSLOG_SEMIHOSTING

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int syslog_semihosting_flush(void)
{
  return OK;
}

#ifdef CONFIG_SYSLOG_WRITE
static int syslog_semihosting_write(FAR const char *buffer, size_t buflen)
{
  char * msg = (char *)malloc(buflen + 1);
  memcpy(msg, buffer, buflen);
  msg[buflen] = '\0';

  up_puts(buffer);
  free(msg);

  return OK;
}
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern int smh_sys_writec(int ch);

/* This structure describes the SYSLOG channel */

static const struct syslog_channel_s g_syslog_semihosting_channel =
{
  smh_sys_writec,
  smh_sys_writec,
  syslog_semihosting_flush,
#ifdef CONFIG_SYSLOG_WRITE
  syslog_semihosting_write,
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: syslog_semihosting_channel
 *
 * Description:
 *   Configure to use ARM semihosting as the SYSLOG channel.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int syslog_semihosting_channel(void)
{
  /* Initialize the semihosting based channel */

  return syslog_channel(&g_syslog_semihosting_channel);
}

#endif /* CONFIG_SYSLOG_SEMIHOSTING */
