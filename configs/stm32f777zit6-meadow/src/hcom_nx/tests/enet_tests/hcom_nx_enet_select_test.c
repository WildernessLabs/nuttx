/*************************************************************************
 * hcom_nx\tests\enet_tests\hcom_nx_enet_select_tests.c
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
#include <nuttx/config.h>
#include "syslog.h"

#include <meadow/hcom_shared_common.h>
#include <meadow/hcom_nuttx_shared.h>

#if defined(HCOM_INCLUDE_EMBEDDED_ETHERNET_TESTS)

#include <meadow/hcom_upd_shared.h>

// Direct the test to the correct code
void hcom_nx_enet_test_selector(struct hcom_nx_cmd_data *cmdData)
{
  int ret;
  int userData = cmdData->userData;

  // Give each test 16 test options
  switch(userData & 0x000000f0)
  {
    case 0x00000000:
      ret = hcom_nx_ethernet_ping_test(cmdData);
    break;

    // case 0x00000010:
    //   return hcom_ethernet_ping6_tests(userData);
    // break;

    default:
      syslog(2, "Undefined userData value:%d\n", userData);
      return;
  }

  if(ret < 0)
  {
    syslog(2, "Something went wrong...\n");
  }
}
#endif // #if defined(HCOM_INCLUDE_EMBEDDED_ETHERNET_TESTS)
