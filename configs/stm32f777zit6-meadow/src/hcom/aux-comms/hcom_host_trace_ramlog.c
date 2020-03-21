/****************************************************************************
 * configs/stm32f777-zit6-meadow/src/hcom/hcom_host_trace_ramlog.c
 *
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
 *   Copyright (C) 2017 Gregory Nutt. All rights reserved.
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

#include "../hcom_common.h"

#include <nuttx/kthread.h>
#include <sys/stat.h>
#include <ctype.h>
#include <poll.h>

#if HCOM_TASK_SHOW_CREATED_TASK_PID_NAME > 0
#include <nuttx/sched.h>
#include <../sched/sched/sched.h>
#endif

#if defined (CONFIG_RAMLOG_SYSLOG)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HCOM_TRACE_RAMLOG_READ_BUFF_SIZE 384

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
#endif

static char *thisFile = __FILE__;

#if defined (CONFIG_RAMLOG_SYSLOG)
static bool _shutting_down;
static int _ramlog_fd;
static bool _trace_logging_requested;
static struct host_com_cir_buffer_s *_ramlog_cbuf;
static bool _trace_ramlog_initialized;
static uint8_t *_singleMsgBuf;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
#ifdef CONFIG_BUILD_PROTECTED
static int hcom_trace_ramlog_kthread(int argc, char *argv[]);
#else
static FAR void *hcom_trace_ramlog_pthread(FAR void *arg);
#endif

static int hcom_trace_ramlog_lazy_initialization(void);
static int hcom_trace_ramlog_make_thread(void);
static int hcom_trace_ramlog_open_ramlog(void);
static void hcom_trace_ramlog_close_and_delay(bool closeNeeded);
static int hcom_trace_ramlog_read_ramlog_loop(void);
static int hcom_trace_ramlog_save_recvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt);
static int hcom_trace_ramlog_pull_all_packets_from_buffer(void);
static int hcom_trace_ramlog_route_trace_text(uint8_t *buffer, int readReturn);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_trace_ramlog_setup()
{
  _shutting_down = false;
  _trace_ramlog_initialized = false;
  _ramlog_fd = -1;
  _trace_logging_requested = false;

  // If tracing needed on restart, initialize now
  if (hcom_utils_bbreg_is_bit_set(HCOM_BATTERY_BACKED_REG_BIT_FLAGS,
          HCOM_BBREG_TRACE_MSG_TO_HOST_BIT_FLAG))
  {
    return hcom_trace_ramlog_lazy_initialization();
  }
  
  return OK;
}

//==========================================================================
// Closing connection forces a receive error which, causes the thread to return.
void hcom_trace_ramlog_shutdown()
{
  _shutting_down = true;

  close(_ramlog_fd);
  _ramlog_fd = -1;

  free(_singleMsgBuf);
  free(_ramlog_cbuf);
}

//==========================================================================
// Because this feature is rarely used we initialize only on request
int hcom_trace_ramlog_lazy_initialization()
{
  int ret;

  if(_trace_ramlog_initialized)
    return OK;

  // Create a thread to read the ramlog
  ret = hcom_trace_ramlog_make_thread();
  if (ret < 0)
  {
    hcom_utils_directlog(LOG_ERR, "%s@%d-thread create, errno:%d\n",
      thisFile, __LINE__, errno);
    return -1;
  }

  // Create a circular buffer to manage message from ramlog
  _ramlog_cbuf = (struct host_com_cir_buffer_s *)malloc(sizeof(struct host_com_cir_buffer_s));
  if (_ramlog_cbuf == NULL)
  {
    hcom_utils_directlog(LOG_ERR, "%s@%d-cir buf alloc\n", thisFile, __LINE__);
    return -1;
  }
    
  // p-m DEFINE THESE HARDCODED CONSTANTS!
  // Initialize the circular buffer
  int result = hcom_cirbuf_init(_ramlog_cbuf,
          HCOM_TRACE_RAMLOG_READ_BUFF_SIZE * 4, 0x0a);
  if (result == HCOM_CIR_BUF_INIT_FAILED)
  {
    hcom_utils_directlog(LOG_ERR, "%s@%d-hcom_cirbuf_init\n", thisFile, __LINE__);
    return -1;
  }
  
  // Final message buffer
  _singleMsgBuf = malloc(HCOM_PROTOCOL_REQUEST_MAX_SIMPLE_DATA_LEN);
  if (_singleMsgBuf == NULL)
  {
    hcom_utils_directlog(LOG_ERR, "%s@%d-cir buf alloc\n", thisFile, __LINE__);
    return -1;
  }

  _trace_ramlog_initialized = true;
  return OK;
}

//=============================================================
int hcom_trace_ramlog_make_thread()
{
  #ifdef CONFIG_BUILD_PROTECTED
    int pid = kthread_create(
      HCOM_THREAD_NAME_TRACE_RAMLOG,
      HCOM_THREAD_PRIORITY_TRACE_RAMLOG,
      2048, (main_t)hcom_trace_ramlog_kthread,
      (FAR char * const *)  NULL);
     if(pid <= 0)
    {
      return -ENOEXEC;
    }

  #else

    int ret;
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;

    param.sched_priority = HCOM_THREAD_PRIORITY_TRACE_RAMLOG;
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setschedparam(&attr, &param);
    (void)pthread_attr_setstacksize(&attr, 1024);

    ret = pthread_create(&thread, &attr, hcom_trace_ramlog_pthread, NULL);
    if (ret < 0)
    {
      hcom_utils_directlog(LOG_CRIT, "%s@%d-Thread create error:%d\n", thisFile, __LINE__, ret);
      return ret;
    }
  #endif

  return OK;
}

//=================================================================
// This thread receives all ramlog messages received from syslog
#ifdef CONFIG_BUILD_PROTECTED
int hcom_trace_ramlog_kthread(int argc, char *argv[])
#else
FAR void *hcom_trace_ramlog_pthread(FAR void *arg)
#endif
{
  int ret;
  
#if HCOM_TASK_SHOW_CREATED_TASK_PID_NAME > 0
  struct tcb_s *rtcb = this_task();
  hcom_utils_f7syslog(LOG_NOTICE, "PID:%d is '%s'\n", getpid(), rtcb->name);
#endif

  while(!_shutting_down)
  {
    ret = hcom_trace_ramlog_open_ramlog();
    if(ret < 0)
    {
      hcom_trace_ramlog_close_and_delay(false);
      continue;
    }

    ret = hcom_trace_ramlog_read_ramlog_loop();
    if(ret < 0)
    {
      hcom_trace_ramlog_close_and_delay(true);
    }
  }

#ifdef CONFIG_BUILD_PROTECTED
  return OK;
#else
  return NULL;    // Keeps compiler happy
#endif
}

//=================================================================
void hcom_trace_ramlog_close_and_delay(bool closeNeeded)
{
  if(closeNeeded)
  {
    close(_ramlog_fd);
    _ramlog_fd = -1;
  }

  // Wait and try again
  if(!_shutting_down)
    sleep(5);   // Not a special value, just no prevent hard infinite looping
}

//=================================================================
int hcom_trace_ramlog_open_ramlog()
{
  if(_ramlog_fd >= 0)
  {
    close(_ramlog_fd);
    _ramlog_fd = -1;
  }

  _ramlog_fd = open(HCOM_TRACE_RAMLOG_DEVICE_NAME, O_RDONLY);
  // p-m for testing with a text file
 //  _ramlog_fd = open("/meadow/RamlogTest.txt", O_RDONLY);
  if (_ramlog_fd < 0)
  {
    hcom_utils_directlog(LOG_ERR, "%s@%d-open %s, errno:%d\n",
          thisFile, __LINE__, HCOM_TRACE_RAMLOG_DEVICE_NAME, errno);
    return -1;
  }

  return OK;
}

//=================================================================
// This function reads the data put into the ramlog.
int hcom_trace_ramlog_read_ramlog_loop()
{
  #define HCOM_TRACE_RAMLOG_POLL_THREADS_POLLING 1
  #define HCOM_TRACE_RAMLOG_POLL_WAIT_DELAY (10 * 1000)   // 10 seconds
  struct pollfd fds[HCOM_TRACE_RAMLOG_POLL_THREADS_POLLING];
  uint8_t buffer[HCOM_TRACE_RAMLOG_READ_BUFF_SIZE];
  ssize_t readReturn;
  int ret;

  memset(fds, 0, sizeof(struct pollfd) * HCOM_TRACE_RAMLOG_POLL_THREADS_POLLING);

  fds[0].fd      = _ramlog_fd;
  fds[0].events  = POLLIN;
  fds[0].revents = 0;

  while (!_shutting_down)
  {
    // Blocks till data is available
    ret = poll(fds, HCOM_TRACE_RAMLOG_POLL_THREADS_POLLING, HCOM_TRACE_RAMLOG_POLL_WAIT_DELAY);
    if (ret < 0 || ret > HCOM_TRACE_RAMLOG_POLL_THREADS_POLLING)
    {
      // Check errno for one of these values
      // EBADF. An invalid file descriptor was given in one of the sets.
      // EFAULT. The fds address is invalid
      // EINTR. A signal occurred before any requested event.
      // EINVAL. The nfds value exceeds a system limit.
      // ENOMEM. There was no space to allocate internal data structures.
      // ENOSYS. One or more of the drivers supporting the file descriptor does not support the poll method.
      hcom_utils_directlog(LOG_ERR, "%s@%d ramlog poll ret:%d errno:%d\n", thisFile, __LINE__, ret, errno);
      continue;
    }
    else if (ret == 0)
    {
      // Timeout - which is being ignored
      DEBUGASSERT(fds[0].revents == 0);
      continue;
    }

    // Data must be available
    DEBUGASSERT(ret == 1);
    DEBUGASSERT(fds[0].revents == POLLIN);    // POLLIN = 0x01

    while(!_shutting_down)
    {
      // Read ramlog
      readReturn = read(_ramlog_fd, buffer, HCOM_TRACE_RAMLOG_READ_BUFF_SIZE);
      if (readReturn < 0 )
      {
        // Error
        hcom_utils_directlog(LOG_ERR, "%s@%d ramlog read readReturn:%d errno:%d\n", thisFile, __LINE__, readReturn, errno);
        return -errno;    // Close connection and try again
      }
      else if (readReturn == 0)
      {
        // All data read, need to wait for poll to indicate more data
        break;
      }
      else
      {
        // Successful ramlog message read put into circular buffer
        // unless logging turned off
        if(_trace_logging_requested)
        {
          ret = hcom_trace_ramlog_save_recvd_data(buffer, readReturn);
          if (ret < 0 )
          {
            return ret;
          }
        }
      }
    }   // while (!_shutting_down)
  }   // while (!_shutting_down)

  return OK;
}

//=======================================================================
// Add the received data to the circular buffer. It can be added byte by byte
// or several messages at once.
int hcom_trace_ramlog_save_recvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt)
{
  int result;

  if (recvByteCnt == 0)
    return OK;

  // This loop is used to add received data to the buffer until no more will fit
  for (;;)
  {
    result = hcom_cirbuf_add_bytes(_ramlog_cbuf, recvBuff, recvByteCnt);
    if(result == HCOM_CIR_BUF_ADD_SUCCESS)
    {
      // hcom_comms_dbg(LOG_DEBUG, "%s@%d-%d bytes added to cir buf\n", thisFile, __LINE__, recvByteCnt);
      break;
    }
    else if (result == HCOM_CIR_BUF_ADD_WONT_FIT)
    {
      // Wasn't possible to put these bytes in the buffer. We need to
      // pull packets and retry to add this data
      hcom_utils_directlog(LOG_WARNING, "%s@%d-No room in cir buf, pull and retry\n",
              thisFile, __LINE__);
      result = hcom_trace_ramlog_pull_all_packets_from_buffer();
      if (result == HCOM_CIR_BUF_GET_FOUND_MSG)
        continue;   // There should be room now for the failed add

      if (result == HCOM_CIR_BUF_GET_NONE_FOUND || result == HCOM_CIR_BUF_GET_DEST_NO_ROOM)
      {
        hcom_utils_directlog(LOG_ERR, "%s@%d-pull packets from cir buf %d\n",
                 thisFile, __LINE__, result);
        return OK;    // Report and throw data away.
      }
    }
    else if (result == HCOM_CIR_BUF_ADD_BAD_ARG)
    {
      hcom_utils_directlog(LOG_ERR, "%s@%d-Bad argument to cir buf\n", thisFile, __LINE__);
      return OK; // Report and throw data away and keep going
    }
    else
    {
      hcom_utils_directlog(LOG_ERR, "%s@%d-Unknown cir buf add err:%d\n", thisFile, __LINE__, result);
      return OK; // Report and throw data away and keep going
    }
  }

  // See if there's 1 or more complete message(s)
  result = hcom_trace_ramlog_pull_all_packets_from_buffer();
  return result;
}

//====================================================================
// Pull and process all the complete packets from the circular buffer
int hcom_trace_ramlog_pull_all_packets_from_buffer()
{
  int result;
  size_t packetLength;

  for (;;)
  {
    // If buffer too small packetLength will contain the desired size
    result = hcom_cirbuf_get_next_packet(_ramlog_cbuf, _singleMsgBuf,
            HCOM_PROTOCOL_REQUEST_MAX_SIMPLE_DATA_LEN, &packetLength);

    if (result == HCOM_CIR_BUF_GET_NONE_FOUND)
      return OK; // Return to receive more data - only way out

    DEBUGASSERT(result != HCOM_CIR_BUF_GET_DEST_NO_ROOM);
    DEBUGASSERT(result == HCOM_CIR_BUF_GET_FOUND_MSG);
      
    // Process the isolated message
    result = hcom_trace_ramlog_route_trace_text(_singleMsgBuf, packetLength);
    if (result == OK)
    {
      continue; // pull next packet
    }
    else if (result == -EAGAIN)
    {
      return OK;    // transmission blocked
    }
    else
    {
      hcom_utils_directlog(LOG_ERR, "%s@%d-processing data:%d\n", thisFile, __LINE__, result);
      return result;
    }
  }
  return OK;
}

//=================================================================
// Ship the ramlog text to host PC
// This is the LAST POSSIBLE place that hcom_utils_directlog can be called  
int hcom_trace_ramlog_route_trace_text(uint8_t *recvBuff, int numbBytes)
{
  if(numbBytes == 0)
    return OK;

  // Look for 0x0d & 0x0a at the end and shorten message length accordingly
  if(recvBuff[numbBytes - 1] == 0x0a || recvBuff[numbBytes - 1] == 0x0d)
    numbBytes--;
  if(recvBuff[numbBytes - 1] == 0x0a || recvBuff[numbBytes - 1] == 0x0d)
    numbBytes--;

  DEBUGASSERT(numbBytes < HCOM_MAX_HOST_STRING_BUFF_LENGTH);

  // Entire message sent, includes ctrl chararacters
  int ret = hcom_comms_send_raw_string_msg(HCOM_HOST_REQUEST_TEXT_TRACE_MSG, 0, (char *) recvBuff,
          numbBytes, thisFile, __LINE__);
  if(ret == -EAGAIN)
    return ret;
    
  return OK;
}
#endif

//======================================================================================
// Called from Meadow.CLI
void hcom_trace_ramlog_send_trace_to_host(uint32_t userData)
{
  // p-m ANY REASON FOR TRACEENABLE TO BE PERSISTED ACROSS RESTARTS?
  // hcom_utils_bbreg_set_bit(HCOM_BATTERY_BACKED_REG_BIT_FLAGS,
  //         HCOM_BBREG_TRACE_MSG_TO_HOST_BIT_FLAG);

#if defined (CONFIG_RAMLOG_SYSLOG)
  // Do ramlog initialization, if needed
  _trace_logging_requested = true;
  hcom_trace_ramlog_lazy_initialization();
#endif

  char *sendMsgToHost = "Trace logs sent to PC";
  hcom_comms_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);
}

//======================================================================================
// Called from Meadow.CLI
void hcom_trace_ramlog_no_trace_msg_to_host(uint32_t userData)
{
  hcom_utils_bbreg_clear_bit(HCOM_BATTERY_BACKED_REG_BIT_FLAGS,
          HCOM_BBREG_TRACE_MSG_TO_HOST_BIT_FLAG);

#if defined (CONFIG_RAMLOG_SYSLOG)
  _trace_logging_requested = false;
#endif

  char *sendMsgToHost = "Trace logs not sent to PC";
  hcom_comms_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);
}
