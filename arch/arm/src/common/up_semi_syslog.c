/****************************************************************************
 * arch/arm/src/common/up_semi_syslog.c
 *
 *   Copyright (C) 2018 Pinecone Inc. All rights reserved.
 *   Copyright (C) 2019 Wilderness Labs All rights reserved.
 *   Author: Xiang Xiao <xiaoxiang@pinecone.net>
 *           Joao Matos <joao@wildernesslabs.co>
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
#include <nuttx/arch.h>

#include <syscall.h>
#include <string.h>

#include "up_internal.h"

#ifdef CONFIG_ARM_SEMIHOSTING_SYSLOG

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SEMI_SYSLOG_OPEN    0x01

/* Open mode flags */
#define SEMI_SYSLOG_FILE_MODE_R          0 
#define SEMI_SYSLOG_FILE_MODE_R_B        1
#define SEMI_SYSLOG_FILE_MODE_R_PLUS     2
#define SEMI_SYSLOG_FILE_MODE_R_PLUS_B   3
#define SEMI_SYSLOG_FILE_MODE_W          4
#define SEMI_SYSLOG_FILE_MODE_W_B        5
#define SEMI_SYSLOG_FILE_MODE_W_PLUS     6
#define SEMI_SYSLOG_FILE_MODE_W_PLUS_B   7
#define SEMI_SYSLOG_FILE_MODE_A          8
#define SEMI_SYSLOG_FILE_MODE_A_B        9
#define SEMI_SYSLOG_FILE_MODE_A_PLUS     10
#define SEMI_SYSLOG_FILE_MODE_A_PLUS_B   11

#define SEMI_SYSLOG_WRITEC  0x03
#define SEMI_SYSLOG_WRITE0  0x04
#define SEMI_SYSLOG_READC   0x07

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: smh_sys_open
 ****************************************************************************/

struct smh_sys_open_param
{
    const char * str;
    int mode;
    int len;
};

int smh_sys_open(FAR const char *str, int mode)
{
  struct smh_sys_open_param param;
  param.str = str;
  param.mode = mode;
  param.len = strlen(str);

  smh_call(SEMI_SYSLOG_OPEN, &param);

  return 0;
}

/****************************************************************************
 * Name: smh_sys_writec
 ****************************************************************************/

int smh_sys_writec(int ch)
{
  smh_call(SEMI_SYSLOG_WRITEC, &ch);
  return ch;
}

/****************************************************************************
 * Name: smh_sys_write0
 ****************************************************************************/

void smh_sys_write0(FAR const char *str)
{
  smh_call(SEMI_SYSLOG_WRITE0, (char *)str);
}

//#ifndef ARCH_SYSLOG

static int smh_stdout_fd = -1;

void smh_init(void)
{
    if (smh_stdout_fd == -1)
        smh_stdout_fd = smh_sys_open(":tt", SEMI_SYSLOG_FILE_MODE_W);
}

/****************************************************************************
 * Name: up_putc
 ****************************************************************************/

int up_putc(int ch)
{
  smh_sys_writec(ch);
  return ch;
}

/****************************************************************************
 * Name: up_puts
 *
 * Description:
 *   Output a string on the console
 *
 ****************************************************************************/

void up_puts(FAR const char *str)
{
  smh_sys_write0(str);
}

//#endif

#endif
