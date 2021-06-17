/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\hcom_nx\diag\hcom_nx_trace_msg_proc.c
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

#include "hcom_nx_upd_diag.h"

#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>

#include <sys/stat.h>
#include <ctype.h>
#include <poll.h>

#include <signal.h>

#include <arch/board/board.h>
#include "stm32_gpio.h"

#define USE_MEADOW_DEBUG_HELPERS
// #undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

#if defined (CONFIG_RAMLOG_SYSLOG)
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HCOM_TRACE_RAMLOG_READ_BUF_SIZE (256)
#define HCOM_TRACE_LOCAL_SYSLOG_CIR_BUF_SIZE (HCOM_TRACE_RAMLOG_READ_BUF_SIZE * 5)
#define HCOM_TRACE_RAMLOG_SERIAL_NAME ("/dev/ttyS0")    // UART 1

#define HCOM_TRACE_SHARED_SYSLOG_CIR_BUF_SIZE HCOM_TRACE_LOCAL_SYSLOG_CIR_BUF_SIZE

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
static host_com_cir_buffer_t *_ramlog_cbuf;
// This buffer is where each messages is placed before being sent out
// via uart or to CLI. When we must send to CLI a semaphore protects
// this shared memory.
static uint8_t *_ramlogMsgBuf;
static int _ramlog_fd;
static int _uart1_fd;
static bool _trace_kthread_running;
static bool _trace_log_to_host;
static bool _trace_log_to_uart1;
static bool _mono_has_started;
static int _uart1_needs_reconfig;
static size_t _cliMsgLength;
static sem_t _readNxtSem;
static sem_t _sendCliSem;
static int _ramlog_reader_kthread_pid;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void *hcom_nx_trace_msg_kthread(int argc, char *argv[]);

static void hcom_nx_trace_read_bbreg_config(void);
static int hcom_nx_trace_msg_make_thread(void);
static int hcom_nx_trace_msg_open_ramlog(void);
static int hcom_nx_trace_msg_lazy_initialization(void);
static void hcom_nx_trace_msg_close_and_delay(bool ramLogClose);
static int hcom_nx_trace_msg_read_ramlog_loop(void);
static int hcom_nx_trace_msg_save_recvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt);
static int hcom_nx_trace_msg_pull_all_packets_from_buffer(void);
static int hcom_nx_trace_msg_route_trace_text(void);
static void hcom_nx_trace_msg_err_logger(int priority, FAR const IPTR char *fmt, ...);
static int hcom_nx_trace_msg_send_msg_to_uart1(const char *sendBuff, size_t numbBytes);
static int hcom_nx_trace_msg_open_uart1_serial_port(void);
static void hcom_nx_trace_msg_wait_sem(sem_t *semaphore);
static void hcom_nx_trace_kthread_exit_initiate(void);
static void hcom_nx_trace_kthread_exit_cleanup(void);
static void hcom_nx_trace_msg_sig_recv(int signo, FAR siginfo_t *info, FAR void *context);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_nx_trace_msg_proc_setup()
{
  _mono_has_started = false;  // mono will only tell us once
  _ramlog_reader_kthread_pid = 0;

  // These are only needed for sending trace to CLI
  sem_init(&_readNxtSem, 0, 0);
  sem_setprotocol(&_readNxtSem, SEM_PRIO_NONE);

  sem_init(&_sendCliSem, 0, 0);
  sem_setprotocol(&_sendCliSem, SEM_PRIO_NONE);

  // Check the BBR to determine if initialization is needed, but
  // only during startup
  hcom_nx_trace_read_bbreg_config();

  // Follow through with initialization if needed
  if(_trace_log_to_host || _trace_log_to_uart1)
    hcom_nx_trace_msg_lazy_initialization();
  return OK;
}

//==========================================================================
// This function reads the battery backed register and sets appropriate flags
void hcom_nx_trace_read_bbreg_config()
{
  int bbrRegValue;

  // If started after nuttx start time recheck the battery
  // backed registersn as it could have been days ago.
  bbrRegValue = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT & bbrRegValue) > 0)
  {
    _trace_log_to_uart1 = true;
  }
  else
  {
    _trace_log_to_uart1 = false;
  // PeterM - THIS VERSION OF CONFIGURATION PROCESSING SOMETIMES LOCKS UP
  //   hcom_nx_config_lock();
  //   meadow_configuration_t *config = hcom_nx_get_configuration();
  //   if (config != NULL)
  //   {
  //     _trace_log_to_uart1 |= (config->use_uart1_for_trace != 0);
  //   }
  //   hcom_nx_config_unlock();
  }

#if HCOM_FORCE_SYSLOG_MASK_F7_AND_UART1 > 0
  _trace_log_to_uart1 = true;
#endif

  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT & bbrRegValue) > 0)
    _trace_log_to_host = true;
  else
    _trace_log_to_host = false;
}

//==========================================================================
// Called when mono starts
int hcom_nx_trace_msg_mono_started()
{
  // Because we cannot know when/if mono has messed with UART1's configuration
  // we'll do it for the first few trace messages after mono has started.
  _mono_has_started = true;
  if(_trace_log_to_uart1)
    _uart1_needs_reconfig = 5;

  return OK;
}

//==========================================================================
// Because this feature is rarely used we initialize only when requested
// May be called at start or from CLI command or both
int hcom_nx_trace_msg_lazy_initialization()
{
  int ret;

  _ramlog_fd = -1;
  _uart1_fd = -1;
  _uart1_needs_reconfig = 0;
  _cliMsgLength = 0;
  _trace_kthread_running = false;
  _ramlog_reader_kthread_pid = 0;

  // Create a circular buffer instance to manage messages read from ramlog
  _ramlog_cbuf = (host_com_cir_buffer_t *)malloc(sizeof(struct host_com_cir_buffer_s));
  if (_ramlog_cbuf == NULL)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-cir buf malloc\n", thisFile, __LINE__);
    return -1;
  }

  // Initialize the internal circular buffer and use '/0' (NULL) as the delimiter. As
  // every string will terminate in this way.
  ret = hcom_cirbuf_init(_ramlog_cbuf, HCOM_TRACE_LOCAL_SYSLOG_CIR_BUF_SIZE, 0x0a);
  if (ret == HCOM_CIR_BUF_INIT_FAILED)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-Cir buf malloc failed\n", thisFile, __LINE__);
    return -1;
  }

  // Message buffer contining full messages pulled from the ramlog
  _ramlogMsgBuf = malloc(HCOM_PROTOCOL_REQUEST_MAX_PAYLOAD_LEN);
  if (_ramlogMsgBuf == NULL)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-cir buf malloc\n", thisFile, __LINE__);
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

  return OK;
}

//=============================================================
// Create a kthread to use as a syslog message reader
int hcom_nx_trace_msg_make_thread()
{
  _ramlog_reader_kthread_pid = kthread_create(HCOM_THREAD_NAME_TRACE_RAMLOG,
                                   HCOM_THREAD_PRIORITY_TRACE_RAMLOG,
                                   HCOM_THREAD_STACKSIZE_TRACE_RAMLOG,
                                   (main_t) hcom_nx_trace_msg_kthread,
                                   (char *const *) NULL);
  if (_ramlog_reader_kthread_pid <= 0)
  {
      return -ENOEXEC;
  }

  return OK;
}

//=================================================================
// This thread reads all ramlog messages received via syslog from
// the ramlog buffer and routes them to the UART1 and CLI. This
// thread once started is not stopped until F7 reset.
// PeterM - TBD Make this killable?
void *hcom_nx_trace_msg_kthread(int argc, char *argv[])
{
  int ret;
  struct sigaction act;

  _shutting_down = false;

  // Attach a signal handler to catch the kill command
  act.sa_sigaction = hcom_nx_trace_msg_sig_recv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);

  ret = sigaction(SIGALRM, &act, NULL);
  if (ret < 0)
  {
    //hcom_logging_syslog(LOG_ERR, "%s@%d-attach signal errno:%d\n", thisFile, __LINE__, errno);
    return;
  }

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(2, "New kthread [PID:%d],'%s'\n", getpid(), HCOM_THREAD_NAME_TRACE_RAMLOG);
#endif

  _trace_kthread_running = true;

  // Only leave this loop when shutting down
  while(!_shutting_down)
  {
    // Must have ramlog, it's the source of all trace info
    ret = hcom_nx_trace_msg_open_ramlog();
    if(_shutting_down) break;
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(false);
      continue;
    }

    // Open uart1 if it's requested
    if(_trace_log_to_uart1)
    {
      ret = hcom_nx_trace_msg_open_uart1_serial_port();
      if(_shutting_down) break;
      if(ret < 0)
      {
        hcom_nx_trace_msg_close_and_delay(false);
        continue;
      }
    }

    // Attempt to read from the ramlog buffer
    ret = hcom_nx_trace_msg_read_ramlog_loop();
    if(_shutting_down) break;
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(true);
    }
  }

  // Time for this thread to exit
  hcom_nx_trace_kthread_exit_cleanup();

  _trace_kthread_running = false;

  return NULL;
}

//=================================================================
// Need signal handler or the default Nuttx behavior kicks in
void hcom_nx_trace_msg_sig_recv(int signo, FAR siginfo_t *info, FAR void *context)
{
}

//==========================================================================
// Initiate the kthread exit.
void hcom_nx_trace_kthread_exit_initiate()
{
  // A pthread cannot kill a kthread so we must indirectly kill the thread
  // by setting _shutting_down true, closing connections and waiting
  _shutting_down = true;

  // Signal the kthread so _shutting_down will be tested, forcing it to exit
  kill(_ramlog_reader_kthread_pid, SIGALRM);

  // Nuttx kthreads have no 'join' functionality. We stay in this loop
  // until the exiting kthread has cleaned up it's resources.
  while(_trace_kthread_running)
  {
    usleep(10 * 1000);
  }
}

//==========================================================================
// This function is called by the kthread just before it exits
// and is used to reclaim resources
void hcom_nx_trace_kthread_exit_cleanup()
{
  // These need to be false in case a restarted comes later
  _trace_log_to_uart1 = false;
  _trace_log_to_host = false;

  close(_ramlog_fd);
  _ramlog_fd = -1;

  close(_uart1_fd);
  _uart1_fd = -1;

  if(_ramlog_cbuf != NULL)
  {
    hcom_cirbuf_release_memory(_ramlog_cbuf);
    free(_ramlog_cbuf);
    _ramlog_cbuf = NULL;
  }
  if(_ramlogMsgBuf != NULL)
  {
    free(_ramlogMsgBuf);
    _ramlogMsgBuf = NULL;
  }
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
  if(_shutting_down) return OK;
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
  if(_shutting_down) return OK;
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
// Returning on error will close the uart connection and re-open it
int hcom_nx_trace_msg_read_ramlog_loop()
{
  uint8_t buffer[HCOM_TRACE_RAMLOG_READ_BUF_SIZE];
  ssize_t readReturn;
  int ret;

  while(!_shutting_down)
  {
    // Read ramlog
    readReturn = read(_ramlog_fd, buffer, HCOM_TRACE_RAMLOG_READ_BUF_SIZE);
    if(_shutting_down) break;
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
      return -errno;    // Close connection, wait and try again
    }
    else
    {
      // Successful ramlog message read. Put message into circular buffer
      // unless all logging has been turned off
      if(_trace_log_to_host || _trace_log_to_uart1)
      {
        ret = hcom_nx_trace_msg_save_recvd_data(buffer, readReturn);
        if (ret < 0 )
        {
          return ret;    // Close connection, wait and try again
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
  int addResult;
  int pullResult;

  if (recvByteCnt == 0)
    return OK;

  // This loop is used to add received data and read all available data to/from
  // the buffer until no more will fit
  while(!_shutting_down)
  {
    // Can return HCOM_CIR_BUF_ADD_SUCCESS, HCOM_CIR_BUF_ADD_WONT_FIT, HCOM_CIR_BUF_ADD_BAD_ARG,
    addResult = hcom_cirbuf_add_bytes(_ramlog_cbuf, recvBuff, recvByteCnt);
    if(addResult == HCOM_CIR_BUF_ADD_SUCCESS)
    {
      break;    // Leave loop and check for data ready to read
    }
    else if (addResult == HCOM_CIR_BUF_ADD_WONT_FIT)
    {
      // The buffer doesn't have room for these bytes. We need to pull
      // messages and retry to add this data
      // Only returns -error, HCOM_CIR_BUF_GET_NONE_FOUND or
      // HCOM_CIR_BUF_GET_DEST_NO_ROOM
      pullResult = hcom_nx_trace_msg_pull_all_packets_from_buffer();
      if (pullResult == HCOM_CIR_BUF_GET_FOUND_MSG)
      {
        // Pulled and processed some message so attempt to add again
        continue;
      }

      if (pullResult == HCOM_CIR_BUF_GET_NONE_FOUND)
      {
        // This makes no sense. Like a buffer full of garbage and no delimiter
        hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-pull packets from cir buf, none found\n",
                 thisFile, __LINE__);
        return OK;    // Reported so throw data away.
      }
      if (pullResult == HCOM_CIR_BUF_GET_DEST_NO_ROOM)
      {

        // The buffer we supplied is too small for the message found
        hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-pull packets from cir buf, no room\n",
                 thisFile, __LINE__);
        return OK;    // Reported so throw data away.
      }
    }
    else if (addResult == HCOM_CIR_BUF_ADD_BAD_ARG)
    {
      // A bad argument is never expected
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-Bad argument to cir buf\n", thisFile, __LINE__);
      return OK; // Report, throw data away and keep going
    }
    else
    {
      if(_shutting_down) break;
      // Undefined error????
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-Unknown cir buf add err:%d\n", thisFile, __LINE__, addResult);
      return OK; // Report, throw data away and keep going
    }
  }   // while(!_shutting_down)

  // Check if there's more complete message(s) buffered
  pullResult = hcom_nx_trace_msg_pull_all_packets_from_buffer();
  return pullResult;
}

//====================================================================
// Pull and process all the complete packets from the circular buffer
// Returns -error or one of the circular buffer get return values
int hcom_nx_trace_msg_pull_all_packets_from_buffer()
{
  int ret;
  size_t packetLength;

  while(!_shutting_down)
  {
    // If buffer too small for the found message packetLength will contain
    // the needed buffer size.
    ret = hcom_cirbuf_get_next_packet(_ramlog_cbuf, _ramlogMsgBuf,
            HCOM_PROTOCOL_REQUEST_MAX_PAYLOAD_LEN, &packetLength);
    if(_shutting_down) break;

    // Any messages available?
    if (ret == HCOM_CIR_BUF_GET_NONE_FOUND)
    {
      return OK; // Buffer empty, return to get more data
    }

    if(ret == HCOM_CIR_BUF_GET_DEST_NO_ROOM)
    {
      // This is never expected, the buffer is too small for the message.
      // Possibly corrupted data....
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-buffer too small. Needed %d\n",
                thisFile, __LINE__, packetLength);
      return OK; // _ramlogMsgBuf too small, throw away data and keep going 
    }
    
    // ret must be HCOM_CIR_BUF_GET_FOUND_MSG
    _cliMsgLength = packetLength;

    // Route the isolated message
    ret = hcom_nx_trace_msg_route_trace_text();
    if(_shutting_down) break;
    if (ret == OK)
    {
      continue; // pull next packet, if there is one
    }
    else
    {
      hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-processing data:%d\n", thisFile, __LINE__, ret);
      return ret;
    }
  }   // while(!_shutting_down)
  
  return HCOM_CIR_BUF_GET_FOUND_MSG;
}

//=================================================================
// Ship the ramlog text message to uart and/or host CLI
// If this function cannot send it, the message is lost....
int hcom_nx_trace_msg_route_trace_text(void)
{
  int ret;

  if(_trace_log_to_uart1)
  {
    ret = hcom_nx_trace_msg_send_msg_to_uart1((char *) _ramlogMsgBuf, _cliMsgLength);
    if(ret < 0)
    {
      // Error text already sent
      return ret;
    }
  }

  if(_trace_log_to_host)
  {
    // Free CLI pthread to send the message
    sem_post(&_sendCliSem);

    // Wait for cli to be sent before getting next message. Because they share
    // the _ramlogMsgBuf buffer along with the uart1 transmission.
    hcom_nx_trace_msg_wait_sem(&_readNxtSem);
  }

  return OK;
}

//==========================================================================
// This is called via udp and provides a pthread, and the buffer for the
// message to CLI. On every call it waits for the next syslog message.
// When a message is found it returns and calls the code to send
// the message to CLI.
size_t hcom_nx_trace_cli_message_transport(char *buff, size_t buffLen)
{
  static bool firstTime = true;
  size_t msgLength;

  // May seem backward but this caller keeps calling. So, the first call must be
  // ignored. Afterward we allow the reader to get the next message.
  if(firstTime)
  {
    firstTime = false;
  }
  else
  {
    // The caller of this function has consumed the message. Therefore, allow
    // thread that gets the next message to resume.
    sem_post(&_readNxtSem);
  }

  // Wait for messge to be provided for CLI
  hcom_nx_trace_msg_wait_sem(&_sendCliSem);

  // Truncate the message if too long for caller's buffer
  if(_cliMsgLength > buffLen)
    msgLength = buffLen;
  else
    msgLength = _cliMsgLength;
  memcpy(buff, _ramlogMsgBuf, msgLength);

  // By returning the message becomes available to userland for display
  return msgLength;
}

//==========================================================================
// Forward the message to the uart1 for transmission
int hcom_nx_trace_msg_send_msg_to_uart1(const char *sendBuff, size_t numbBytes)
{
  // If uart1 not opened do this now
  if(_uart1_fd < 0)
  {
    // The port must be opened
    // Returns < 0 if cannot open port
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

  // Write string out the uart
  ssize_t nbytes = write(_uart1_fd, sendBuff, numbBytes);
  if (nbytes < 0)
  {
    hcom_nx_trace_msg_err_logger(LOG_ERR, "%s@%d-uart failed to write:%s, errno:%d\n",
             thisFile, __LINE__, HCOM_TRACE_RAMLOG_SERIAL_NAME, errno);
    return nbytes;
  }

  return OK;
}

//===========================================================================
// Wait for the thread holding the semaphore to release it
void hcom_nx_trace_msg_wait_sem(sem_t *semaphore)
{
  int ret;  
  DEBUGASSERT(semaphore != NULL);
  do
  {
    ret = sem_wait(semaphore);    // Take the semaphore (perhaps waiting)
    // The only case that an error should occur here is if the wait was awakened by a signal
    DEBUGASSERT(ret == OK || ret == -EINTR);
  }
  while (ret == -EINTR);
}

//==========================================================================
// Send a string directly to uart1. The priority value is not used but makes
// it like syslog so that this can be used as a substitute for syslog.
// Note:This can only be called from a kthread calling from a user pthread
// will crash Nuttx.
// PeterM-I THINK THERE"S A BUG HERE IF THE UART HAS NOT BEEN OPENED YET
void hcom_nx_uart1_direct(int priority, const char *fmt, ...)
{
#define HCOM_NX_UART1_DIRECT_BUF_LEN (256)
  va_list ap;
  int stringLen;
  char completeStr[HCOM_NX_UART1_DIRECT_BUF_LEN];

  // Create the complete message
  va_start(ap, fmt);
  // Returned value is the string length, including '\0'. But,
  // if buffer is too small the return value is needed buffer
  // length excluding terminating '\0'. But, sending direct to
  // uart means a '\0' is not needed...
  stringLen = vsnprintf(completeStr, HCOM_NX_UART1_DIRECT_BUF_LEN, fmt, ap);
  va_end(ap);

  // Trucated?
  if(stringLen > HCOM_NX_UART1_DIRECT_BUF_LEN)
    stringLen = HCOM_NX_UART1_DIRECT_BUF_LEN;

  hcom_nx_trace_msg_send_msg_to_uart1(completeStr, stringLen);
  
  // Need a carrage return which we usually don't add
  hcom_nx_trace_msg_send_msg_to_uart1("\r", 1);
}

// PeterM - duplicate code here! ^ and v

//==========================================================================
// There's really isn't a good way to handle errors with ramlog because errors
// cannot be written to syslog or we'll end up with an infinite loop. So,
// the best we can do is sent them to the host, and hope it's listening.

// PeterM PLEASE REVISIT THIS. CAN THE UART1 DIRECT/CLI DIRECT BE USED HERE?
// PeterM CAN THE SAME APPROACH BE USED ON THE USER SIDE. NEED TO ADD HCOM_UPD
// SUPPORT FOR THIS.
void hcom_nx_trace_msg_err_logger(int priority, FAR const IPTR char *fmt, ...)
{
  va_list ap;
  int stringLen;
  char completeStr[HCOM_NX_UART1_DIRECT_BUF_LEN];

  // Create the complete message
  va_start(ap, fmt);
  // Returned value is the string length, including '\0'. But,
  // if buffer is too small the return value is needed buffer
  // length excluding terminating '\0'. But, sending direct to
  // uart means a '\0' is not needed...
  stringLen = vsnprintf(completeStr, HCOM_NX_UART1_DIRECT_BUF_LEN, fmt, ap);
  va_end(ap);

  // Trucated?
  if(stringLen > HCOM_NX_UART1_DIRECT_BUF_LEN)
    stringLen = HCOM_NX_UART1_DIRECT_BUF_LEN;

  hcom_nx_trace_msg_send_msg_to_uart1(completeStr, stringLen);
  
  // Need a carrage return which we usually don't add
  hcom_nx_trace_msg_send_msg_to_uart1("\r", 1);
}
#endif    // #if defined (CONFIG_RAMLOG_SYSLOG)

//======================================================================================
// The following functions are always built. They are used to process
// hcom commands received from CLI that control the tracing feature.
//======================================================================================
//
// Called from Meadow.CLI to enable tracing to host.
int hcom_nx_exec_trace_do_send_to_host(struct hcom_nx_cmd_data *cmdData)
{
  // Set the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  _trace_log_to_host = true;

  // Sending syslogs already to UART1? If not we must initialize.
  if(!_trace_log_to_uart1)
    hcom_nx_trace_msg_lazy_initialization();

  char *sendMsgToHost = "Trace logs will be sent to CLI";
#else
  char *sendMsgToHost = "Trace logs cannot be sent to CLI, tracing not available";
#endif
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);
  return OK;
}

//======================================================================================
// Called from Meadow.CLI for both ramlog and syslog
int hcom_nx_exec_trace_do_not_send_to_host(struct hcom_nx_cmd_data *cmdData)
{
  // Clear the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  // Cause the waiting transport pthread to return to userland and exit
  sem_post(&_sendCliSem);

  // Disable ramlogs to host
  _trace_log_to_host = false;
  
  if(!_trace_log_to_uart1)
  {
    // Since no longer needed, terminate the main ramlog reader thread
    // and clean up resources
    hcom_nx_trace_kthread_exit_initiate();
  }
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
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_trace_log_to_uart1)
    return OK;

  // Any need to initialize trace processing?
  _trace_log_to_uart1 = true;

  if(_mono_has_started)
    _uart1_needs_reconfig = 1;  // Allow the first message to reconfigure UART1

  // Sending syslogs already to host? If not we must initialize.
  if(!_trace_log_to_host)
  {
    hcom_nx_trace_msg_lazy_initialization();
  }

  char *sendMsgToHost = "Trace logs will be sent via UART1";
#else
  char *sendMsgToHost = "Trace logs cannot be sent to UART1, trace logging not available";
#endif

  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0, sendMsgToHost,
          thisFile, __LINE__);

  return OK;
}

//======================================================================================
// Called from Meadow.CLI for both ramlog and syslog
int hcom_nx_exec_trace_do_not_send_to_uart1(struct hcom_nx_cmd_data *cmdData)
{
  // Clear the appropriate battery backed register bit
  // PeterM hcom_bbreg_clear_bbr_bits(HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT);
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART1_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  if(!_trace_log_to_uart1)
    return OK;    // Already disabled

  _trace_log_to_uart1 = false;

  if(!_trace_log_to_host)
  {
    // Since no longer needed, terminate the main ramlog reader thread
    // and clean up resources
    hcom_nx_trace_kthread_exit_initiate();
  }
#endif

  
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
            "UART1 is available for .Net Apps", thisFile, __LINE__);
  
  return OK;
}

