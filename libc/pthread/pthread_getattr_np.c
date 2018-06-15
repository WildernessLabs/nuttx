/****************************************************************************
 * libc/pthread/pthread_getattr_np.c
 *
 *   Copyright (C) 2018 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
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

#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

extern const pthread_attr_t g_default_pthread_attr;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  pthread_getattr_np
 *
 * Description:
 *
 * Parameters:
 *   thread_id
 *   attr
 *
 * Return Value:
 *   0 if successful.  Otherwise, an error code.
 *
 * Assumptions:
 *
 ****************************************************************************/
int pthread_getattr_np(pthread_t thread_id, FAR pthread_attr_t *attr)
{
  if (!attr)
  {
      linfo("attr cannot be NULL. Returning %d\n", EINVAL);
      return EINVAL;
  }

  /* init output */
  memcpy(attr, &g_default_pthread_attr, sizeof(pthread_attr_t));

  /* get task control block (TCB) from thread ID */
	FAR struct pthread_tcb_s *thread = (FAR struct pthread_tcb_s *)sched_gettcb((pid_t)thread_id);
	if (!thread)
	{
    linfo("Invalid thread_id. Returning %d\n", ERROR);
		return ERROR;
	}
	
	/* Only these types are allowed?? */
	if (
    (thread->cmn.flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_TASK
    ||
    (thread->cmn.flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_PTHREAD
    ||
    (thread->cmn.flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_KERNEL)
	{
    linfo("Unsupported thread type: %d. Returning %d\n", thread->cmn.flags, ERROR);
		return ERROR;
	}
 
  /* populate attr based on thread info */
 	attr->stacksize = thread->cmn.adj_stack_size;
  attr->priority = thread->cmn.sched_priority;

#ifdef CONFIG_SMP
  attr->affinity = thread->cmn.affinity;
#endif  

  linfo("Returning %d\n", OK);
  return OK;
}


