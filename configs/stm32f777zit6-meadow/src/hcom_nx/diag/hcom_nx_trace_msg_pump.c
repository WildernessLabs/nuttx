/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\hcom_nx\diag\hcom_nx_trace_msg_pump.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#include <meadow/hcom_protocol.h>
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/meadow_cirbuf.h>
#include <meadow/hcom_bbreg_defn.h>
#include "../hcom_nx_config_manager.h"

#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>

#include <sys/stat.h>
#include <ctype.h>
#include <poll.h>

#include <arch/board/board.h>
#include "stm32_gpio.h"

#if defined (CONFIG_RAMLOG_SYSLOG)
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HCOM_TRACE_RAMLOG_READ_BUFF_SIZE 256
#define HCOM_TRACE_CIRCULAR_BUFFER_SIZE (HCOM_TRACE_RAMLOG_READ_BUFF_SIZE * 5)
#define HCOM_TRACE_RAMLOG_SERIAL_NAME ("/dev/ttyS0")    // UART 1

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
#endif

static char *thisFile = __FILE__;

// This covers most of this file
#if defined (CONFIG_RAMLOG_SYSLOG)

static bool _shutting_down;
static struct host_com_cir_buffer_s *_ramlog_cbuf;
static uint8_t *_singleMsgBuf;
static int _ramlog_fd;
static int _uart1_fd;
static bool _trace_ramlog_initialized;
static bool _trace_ramlog_to_host;
static bool _trace_ramlog_to_uart1;
static bool _mono_has_started;
static int _uart1_needs_reconfig;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void *hcom_nx_trace_msg_kthread(int argc, char *argv[]);

static int hcom_nx_trace_msg_make_thread(void);
static int hcom_nx_trace_msg_open_ramlog(void);
static int hcom_nx_trace_msg_lazy_initialization(bool startup);
static void hcom_nx_trace_msg_close_and_delay(bool ramLogClose);
static int hcom_nx_trace_msg_read_ramlog_loop(void);
static int hcom_nx_trace_msg_save_recvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt);
static int hcom_nx_trace_msg_pull_all_packets_from_buffer(void);
static int hcom_nx_trace_msg_route_trace_text(uint8_t *buffer, int readReturn);
static void hcom_nx_trace_msg_err_logger(int priority, FAR const IPTR char *fmt, ...);
static int hcom_nx_trace_msg_send_msg_to_uart1(char *sendBuff, size_t numbBytes);
static int hcom_nx_trace_msg_open_uart1_serial_port(void);

static int hcom_host_send_raw_string_msg_XXX(uint16_t requestType, uint32_t userData, char *shortText,
          size_t msgLength, char *sourceFileName, int sourceLineNumber);
          
// pwm - these were public in hcom_common.h
#if defined (CONFIG_RAMLOG_SYSLOG)
  // int hcom_diag_trace_ramlog_setup(void);
  // void hcom_diag_trace_ramlog_shutdown(void);
  // void hcom_diag_trace_ramlog_mono_started(void);
#endif
// pwm
  // void hcom_diag_trace_forward_to_host(uint32_t userData);
  // void hcom_diag_trace_do_not_send_to_host(uint32_t userData);
  // void hcom_diag_trace_forward_to_uart1(uint32_t userData);
  // void hcom_diag_trace_do_not_send_to_uart1(uint32_t userData);

// pwm - these were public in hcom_common.h

// New stuff

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_nx_trace_msg_pump_setup()
{
  _shutting_down = false;
  _ramlog_fd = -1;
  _uart1_fd = -1;
  _trace_ramlog_initialized = false;
  _mono_has_started = false;
  _uart1_needs_reconfig = 0;

  int bbrRegValue = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT & bbrRegValue) > 0)
  {
    _trace_ramlog_to_uart1 = true;
  }
  else
  {
    hcom_nx_config_lock();
    meadow_configuration_t *config = hcom_nx_get_configuration();
    if (config != NULL)
    {
      _trace_ramlog_to_uart1 |= (config->use_uart1_for_trace != 0);
    }
    hcom_nx_config_unlock();
  }

#if HCOM_FORCE_SYSLOG_MASK_F7_AND_UART1 > 0
  _trace_ramlog_to_uart1 = true;
#endif

  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT & bbrRegValue) > 0)
    _trace_ramlog_to_host = true;
  else
    _trace_ramlog_to_host = false;
    
  // If either enabled initialize
  if(_trace_ramlog_to_uart1 || _trace_ramlog_to_host)
  {
    // The only way to undo this initialization is to restart
    hcom_nx_trace_msg_lazy_initialization(true);
  }

  return OK;
}

//==========================================================================
// Closing connection forces a receive error which, causes the thread to return.
// Currently no way to detect that nuttx is shutting down
// void hcom_nx_trace_msg_shutdown()
// {
//   _shutting_down = true;

//   if(_ramlog_fd > -1)
//   {
//     close(_ramlog_fd);
//     _ramlog_fd = -1;
//   }

//   if(_uart1_fd > -1)
//   {
//     close(_uart1_fd);
//     _uart1_fd = -1;
//   }

//   if(_singleMsgBuf != NULL)
//     free(_singleMsgBuf);

//   if(_ramlog_cbuf != NULL)
//   {
//     hcom_cirbuf_release_memory(_ramlog_cbuf);
//     free(_ramlog_cbuf);
//   }
// }

//==========================================================================
// Called when mono starts
int hcom_nx_trace_msg_mono_started()
{
  syslog(1, "+++>> In '%s' and '%s' was called\n", __FILE__, __func__);
  // Because we cannot know when/if mono has messed with UART1's configuration
  // we'll do it for the first few trace messages after mono has started.
  _mono_has_started = true;
  if(_trace_ramlog_to_uart1)
    _uart1_needs_reconfig = 5;

  return OK;
}

//==========================================================================
// Because this feature is rarely used we initialize only when needed
int hcom_nx_trace_msg_lazy_initialization(bool startup)
{
  int ret;

  // Maybe called at start or from CLI command
  if(_trace_ramlog_initialized)
  {
    return OK;
  }

  // Create a circular buffer instance to manage messages read from ramlog
  _ramlog_cbuf = (struct host_com_cir_buffer_s *)kmm_malloc(sizeof(struct host_com_cir_buffer_s));
  if (_ramlog_cbuf == NULL)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-cir buf kmm_malloc\n", thisFile, __LINE__);
    return -1;
  }

  // Initialize the circular buffer
  int result = hcom_cirbuf_init(_ramlog_cbuf,
          HCOM_TRACE_CIRCULAR_BUFFER_SIZE, 0x0a);
  if (result == HCOM_CIR_BUF_INIT_FAILED)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-hcom_cirbuf_init\n", thisFile, __LINE__);
    return -1;
  }

  // Host message buffer
  _singleMsgBuf = kmm_malloc(HCOM_PROTOCOL_REQUEST_MAX_PAYLOAD_LEN);
  if (_singleMsgBuf == NULL)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-cir buf kmm_malloc\n", thisFile, __LINE__);
    return -1;
  }

  // Create a thread to read the ramlog
  ret = hcom_nx_trace_msg_make_thread();
  if (ret < 0)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-thread create, errno:%d\n",
              thisFile, __LINE__, errno);
    return -1;
  }

  _trace_ramlog_initialized = true;
  return OK;
}

//=============================================================
// Create a kthread to use as a syslog pump
int hcom_nx_trace_msg_make_thread()
{
    int thread_id = kthread_create(HCOM_THREAD_NAME_TRACE_RAMLOG,
                                   HCOM_THREAD_PRIORITY_TRACE_RAMLOG,
                                   HCOM_THREAD_STACKSIZE_TRACE_RAMLOG,
                                   (main_t) hcom_nx_trace_msg_kthread,
                                   (char *const *) NULL);
    if (thread_id <= 0)
    {
        return -ENOEXEC;
    }

  return OK;
}

//=================================================================
// This thread reads all ramlog messages received via syslog from
// the ramlog buffer
void *hcom_nx_trace_msg_kthread(int argc, char *argv[])
{
  int ret;
  
#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(2, "New kthread [PID:%d],'%s'\n", getpid(), HCOM_THREAD_NAME_TRACE_RAMLOG);
#endif

  // Never leave this loop until shutdown
  while(!_shutting_down)
  {
    // Must have ramlog, it's the source of all trace data
    ret = hcom_nx_trace_msg_open_ramlog();
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(false);
      continue;
    }

    // Open uart1 if it's requested
    if(_trace_ramlog_to_uart1)
    {
      ret = hcom_nx_trace_msg_open_uart1_serial_port();
      if(ret < 0)
      {
        hcom_nx_trace_msg_close_and_delay(false);
        continue;
      }
    }

    // Attempt to read from the ramlog buffer
    // Stay in this loop unless something goes wrong
    ret = hcom_nx_trace_msg_read_ramlog_loop();
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(true);
    }
  }

  return NULL;
}

//=================================================================
void hcom_nx_trace_msg_close_and_delay(bool ramLogClose)
{
  if(ramLogClose)
  {
    close(_ramlog_fd);
    _ramlog_fd = -1;
  }

  if(_uart1_fd > -1)
  {
    close(_uart1_fd);
    _uart1_fd = -1;
  }

  // Wait and try again
  if(!_shutting_down)
    usleep(100 * 1000);   // Not a special value, just prevent hard infinite looping
}

//=================================================================
int hcom_nx_trace_msg_open_ramlog()
{
  if(_ramlog_fd > -1)
  {
    close(_ramlog_fd);
    _ramlog_fd = -1;
  }

  _ramlog_fd = open(HCOM_TRACE_RAMLOG_DEVICE_NAME, O_RDONLY);
  if (_ramlog_fd < 0)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-open %s, errno:%d\n",
            thisFile, __LINE__, HCOM_TRACE_RAMLOG_DEVICE_NAME, errno);
    return -1;
  }

  return OK;
}

//=================================================================
int hcom_nx_trace_msg_open_uart1_serial_port()
{
  if(_uart1_fd > -1)
  {
    close(_uart1_fd);
    _uart1_fd = -1;
  }

  _uart1_fd = open(HCOM_TRACE_RAMLOG_SERIAL_NAME, O_WRONLY);
  if (_uart1_fd < 0)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-open %s, errno:%d\n",
            thisFile, __LINE__, HCOM_TRACE_RAMLOG_SERIAL_NAME, errno);
    _uart1_fd = -1;
    return -1;
  }
  return OK;
}

//=================================================================
// This function reads the data put into the ramlog.
int hcom_nx_trace_msg_read_ramlog_loop()
{
  uint8_t buffer[HCOM_TRACE_RAMLOG_READ_BUFF_SIZE];
  ssize_t readReturn;
  int ret;

  while(!_shutting_down)
  {
    // Read ramlog
    readReturn = read(_ramlog_fd, buffer, HCOM_TRACE_RAMLOG_READ_BUFF_SIZE);
    if (readReturn < 0 )
    {
      // Error
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d ramlog read readReturn:%d errno:%d\n",
              thisFile, __LINE__, readReturn, errno);
      return -errno;    // Close connection, wait and try again
    }
    else if (readReturn == 0)
    {
      // EOF
      hcom_nx_trace_msg_err_logger(LOG_WARNING, "%s@%d ramlog read EOF read returned:%d errno:%d\n",
              thisFile, __LINE__, readReturn, errno);
      return -1;
    }
    else
    {      
      // Successful ramlog message read. Put message into circular buffer
      // unless logging has been turned off
      if(_trace_ramlog_to_host || _trace_ramlog_to_uart1)
      {
        ret = hcom_nx_trace_msg_save_recvd_data(buffer, readReturn);
        if (ret < 0 )
        {
          return ret;
        }
      }
    }
  }   // while (!_shutting_down)

  return OK;
}

//=======================================================================
// Add the data read from ramlog into the circular buffer. It can be added
// byte by byte or several bytes at once.
// We assume that the message "unit" (aka line) will be delimited with
// 0x0a ('line feed' or '\n')
int hcom_nx_trace_msg_save_recvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt)
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
      break;
    }
    else if (result == HCOM_CIR_BUF_ADD_WONT_FIT)
    {
      // Wasn't possible to put these bytes in the buffer, it's full. We need to
      // pull messages and retry to add this data
      hcom_nx_trace_msg_err_logger(LOG_WARNING, "%s@%d-No room in cir buf, pull and retry\n",
              thisFile, __LINE__);

      // Pull messages
      result = hcom_nx_trace_msg_pull_all_packets_from_buffer();
      if (result == HCOM_CIR_BUF_GET_FOUND_MSG)
      {
        // Pull and processed some so attempt to add again
        continue;
      }
      
      if (result == HCOM_CIR_BUF_GET_NONE_FOUND)
      {
        // This looks like a buffer full of garbage and not delimiter
        hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-pull packets from cir buf, none found\n",
                 thisFile, __LINE__);
        
        return OK;    // Report and throw data away.
      }

      if (result == HCOM_CIR_BUF_GET_DEST_NO_ROOM)
      {
        // The buffer we supplied is too small for the message found
        hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-pull packets from cir buf, no room\n",
                 thisFile, __LINE__, result);

        return OK;    // Report and throw data away.
      }
    }
    else if (result == HCOM_CIR_BUF_ADD_BAD_ARG)
    {
      // A bad argument is never expected
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-Bad argument to cir buf\n", thisFile, __LINE__);
      return OK; // Report and throw data away and keep going
    }
    else
    {
      // Undefined error????
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-Unknown cir buf add err:%d\n", thisFile, __LINE__, result);
      return OK; // Report and throw data away and keep going
    }
  }

  // See if there's 1 or more complete message(s)
  result = hcom_nx_trace_msg_pull_all_packets_from_buffer();
  return result;
}

//====================================================================
// Pull and process all the complete packets from the circular buffer
int hcom_nx_trace_msg_pull_all_packets_from_buffer()
{
  int result;
  size_t packetLength;

  for (;;)
  {
    // If buffer too small for the found message packetLength will contain the desired
    // size. We've sized the buffer large enough that this should never happen.
    result = hcom_cirbuf_get_next_packet(_ramlog_cbuf, _singleMsgBuf,
            HCOM_PROTOCOL_REQUEST_MAX_PAYLOAD_LEN, &packetLength);

    if (result == HCOM_CIR_BUF_GET_NONE_FOUND)
      return OK; // Return to receive more data - only way out

    DEBUGASSERT(result != HCOM_CIR_BUF_GET_DEST_NO_ROOM);
    DEBUGASSERT(result == HCOM_CIR_BUF_GET_FOUND_MSG);

    // Process the isolated message
    result = hcom_nx_trace_msg_route_trace_text(_singleMsgBuf, packetLength);
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
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-processing data:%d\n", thisFile, __LINE__, result);
      return result;
    }
  }
  return OK;
}

//=================================================================
// Ship the ramlog text directly to uart and/or host PC
int hcom_nx_trace_msg_route_trace_text(uint8_t *recvBuff, int numbBytes)
{
  if(_trace_ramlog_to_uart1)
  {
    // Route to UART1
    hcom_nx_trace_msg_send_msg_to_uart1((char *) recvBuff, numbBytes);
  }

  if(_trace_ramlog_to_host)
  {
    // Route to the host PC
    // Strip off 0x0d & 0x0a at the end and shorten length accordingly
    if(recvBuff[numbBytes - 1] == 0x0a || recvBuff[numbBytes - 1] == 0x0d)
      numbBytes--;
    if(recvBuff[numbBytes - 1] == 0x0a || recvBuff[numbBytes - 1] == 0x0d)
      numbBytes--;

    DEBUGASSERT(numbBytes < HCOM_PROTOCOL_REQUEST_MAX_PAYLOAD_LEN);
// pwm
    // Send entire message, includes ctrl chararacters
    int ret = hcom_host_send_raw_string_msg_XXX(HCOM_HOST_REQUEST_TEXT_TRACE_MSG, 0, (char *) recvBuff,
            numbBytes, thisFile, __LINE__);
    if(ret == -EAGAIN)
      return ret;
  }

  return OK;
}

//==========================================================================
// Forward the message to the uart1 for transmission
int hcom_nx_trace_msg_send_msg_to_uart1(char *sendBuff, size_t numbBytes)
{
  // If uart1 not opened do this now
  if(_uart1_fd < 0)
  {
    int ret = hcom_nx_trace_msg_open_uart1_serial_port();
    if(ret < 0)
    {
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-uart open call:%s, errno:%d\n",
              thisFile, __LINE__, HCOM_TRACE_RAMLOG_SERIAL_NAME, errno);
      return ret;
    }
  }

  // For a few messages after mono starts reconfigures UART1
  if(_uart1_needs_reconfig)
  {
    _uart1_needs_reconfig--;

    // Reconfigure uart1
    stm32_configgpio(GPIO_USART1_TX); // PB14
    stm32_configgpio(GPIO_USART1_RX); // PH13
  }

  ssize_t nbytes = write(_uart1_fd, sendBuff, numbBytes);
  if (nbytes < 0)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-uart failed to write:%s, errno:%d\n",
             thisFile, __LINE__, HCOM_TRACE_RAMLOG_SERIAL_NAME, errno);
    return nbytes;
  }
  return OK;
}

//==========================================================================
// THESE ARE PLACE HOLDERS
//==========================================================================
// Send a trace message message to CLI
static void hcom_logging_safe_ramlog(int priority, FAR const IPTR char *fmt, ...)
{

}

// Send a user defined string to CLI
int hcom_host_send_raw_string_msg_XXX(uint16_t requestType, uint32_t userData, char *shortText,
          size_t msgLength, char *sourceFileName, int sourceLineNumber)
{
  return OK;
}

//==========================================================================

//==========================================================================
// There's really isn't a good way to handle errors with ramlog because errors
// cannot be written to syslog or we'll end up with an infinite loop. So,
// the best we can do is sent them to the host, and hope it's listening.
void hcom_nx_trace_msg_err_logger(int priority, FAR const IPTR char *fmt, ...)
{
    if(!(_trace_ramlog_to_uart1 || _trace_ramlog_to_host))
    return;

  va_list args;
  va_start(args, fmt);
  hcom_logging_safe_ramlog(priority, fmt, args);
  va_end(args);  
}

// pwm TODO
  // // Send entire message, includes ctrl chararacters
  // int ret = hcom_host_send_raw_string_msg(HCOM_HOST_REQUEST_TEXT_TRACE_MSG, 0, (char *) recvBuff,
  //         numbBytes, thisFile, __LINE__);
  // if(ret == -EAGAIN)
  //   return ret;
#endif    // #if defined (CONFIG_RAMLOG_SYSLOG)

//======================================================================================
// The following functions are always built. They are used to
// process hcom commands received from CLI
//
// Called from Meadow.CLI to enable tracing to host.
int hcom_nx_exec_trace_forward_to_host(struct hcom_nx_cmd_data *cmdData)
{
  // Set the appropriate battery backed register bit
  // pwm hcom_bbreg_set_bbr_bits(HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT);
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  _trace_ramlog_to_host = true;  // Enable on ramlogs to host

  hcom_nx_trace_msg_lazy_initialization(false);
#endif

  char *sendMsgToHost = "Trace logs to be sent to CLI";
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);
  
  return OK;
}

//======================================================================================
// Called from Meadow.CLI for both ramlog and syslog
int hcom_nx_exec_trace_do_not_send_to_host(struct hcom_nx_cmd_data *cmdData)
{
  // Clear the appropriate battery backed register bit
  // pwm hcom_bbreg_clear_bbr_bits(HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT);
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  // Turn off ramlogs to host
  _trace_ramlog_to_host = false;
#endif

  char *sendMsgToHost = "Trace logs no longer sent to CLI";
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);
  
  return OK;
}

//======================================================================================
// Called from Meadow.CLI to enable tracing to uart1.
int hcom_nx_exec_trace_forward_to_uart1(struct hcom_nx_cmd_data *cmdData)
{
  // Set the appropriate battery backed register bit
  // pwm hcom_bbreg_set_bbr_bits(HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT);
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  _trace_ramlog_to_uart1 = true;  // Enable on ramlogs to uart1

  if(_mono_has_started)
    _uart1_needs_reconfig = 1;  // Allow the first message to reconfigure UART1

  // Initialize if needed
  hcom_nx_trace_msg_lazy_initialization(false);
#endif

  char *sendMsgToHost = "Trace logs will be sent via UART1";
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);
  
  return OK;
}

//======================================================================================
// Called from Meadow.CLI for both ramlog and syslog
int hcom_nx_exec_trace_do_not_send_to_uart1(struct hcom_nx_cmd_data *cmdData)
{
  // Clear the appropriate battery backed register bit
  // pwm hcom_bbreg_clear_bbr_bits(HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT);
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  _trace_ramlog_to_uart1 = false;
#endif

  char *sendMsgToHost = "UART1 is available for .Net Apps";
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);
  
  return OK;
}

