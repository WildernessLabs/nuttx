/****************************************************************************
 * net/usrsock/usrsock_sockif.c
 *
 *  Copyright (C) 2017 Haltian Ltd. All rights reserved.
 *  Author: Jussi Kivilinna <jussi.kivilinna@haltian.com>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/net/net.h>

#include "espcp_usrsock.h"
#include <sys/socket.h>

#include "espcp_coprocessor.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int        espcp_usrsock_sockif_setup(FAR struct socket *psock,
                                       int protocol);
static sockcaps_t espcp_usrsock_sockif_sockcaps(FAR struct socket *psock);
static void       espcp_usrsock_sockif_addref(FAR struct socket *psock);
static ssize_t    espcp_usrsock_sockif_send(FAR struct socket *psock,
                                      FAR const void *buf, size_t len,
                                      int flags);
static int        espcp_usrsock_sockif_close(FAR struct socket *psock);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/*
 *  Table of function pointers for the ESP32 networking methods.
 */
const struct sock_intf_s g_usrsock_sockif_esp32 =
{
  espcp_usrsock_sockif_setup,       /* si_setup */
  espcp_usrsock_sockif_sockcaps,    /* si_sockcaps */
  espcp_usrsock_sockif_addref,      /* si_addref */
  espcp_usrsock_bind,               /* si_bind */
  espcp_usrsock_getsockname,        /* si_getsockname */
  espcp_usrsock_getpeername,        /* si_getpeername */
  espcp_usrsock_listen,             /* si_listen */
  espcp_usrsock_connect,            /* si_connect */
  espcp_usrsock_accept,             /* si_accept */
#ifndef CONFIG_DISABLE_POLL
  espcp_usrsock_poll,               /* si_poll */
#endif
  espcp_usrsock_sockif_send,        /* si_send */
  espcp_usrsock_sendto,             /* si_sendto */
#ifdef CONFIG_NET_SENDFILE
  NULL,                             /* si_sendfile */
#endif
  espcp_usrsock_recvfrom,           /* si_recvfrom */
  espcp_usrsock_sockif_close,       /* si_close */
  espcp_usrsock_ioctl,              /* si_ioctl */
  /*
   *  All of the above are from the generic usrsock project.
   *  The methods below are specific to the Meadow F7 project.
   */
  espcp_usrsock_setsockopt,          /* si_setsockopt */
  espcp_usrsock_getsockopt           /* si_getsockopt */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/*
 *  Static pointer to the name of the file being compiled.  This is used for
 *  logging and making it a static variable ensure that one one instance exists.
 */
static char *_thisFile = __FILE__;

/****************************************************************************
 * Name: espcp_usrsock_sockif_setup
 *
 * Description:
 *   Called for socket() to verify that the provided socket type and
 *   protocol are usable by this address family.  Perform any family-
 *   specific socket fields.
 *
 * Input Parameters:
 *   psock    - A pointer to a user allocated socket structure to be
 *              initialized.
 *   protocol - (see sys/socket.h)
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  Otherwise, a negated errno value is
 *   returned.
 *
 ****************************************************************************/
static int espcp_usrsock_sockif_setup(FAR struct socket *psock, int protocol)
{
  int domain = psock->s_domain;
  int type = psock->s_type;

  psock->s_type = PF_UNSPEC;
  psock->s_conn = NULL;

  /* Let the user socket logic handle the setup...
   *
   * A return value of zero means that the operation was
   * successfully handled by usrsock.  A negative value means that
   * an error occurred.  The special error value -ENETDOWN means
   * that usrsock daemon is not running.  The caller should attempt
   * to open socket with kernel networking stack in this case.
   */

  int ret = espcp_usrsock_socket(domain, type, protocol, psock);
  if (ret == -ENETDOWN)
    {
      nwarn("WARNING: usrsock daemon is not running\n");
    }

  return OK;
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_sockcaps
 *
 * Description:
 *   Return the bit encoded capabilities of this socket.
 *
 * Input Parameters:
 *   psock - Socket structure of the socket whose capabilities are being
 *           queried.
 *
 * Returned Value:
 *   The non-negative set of socket cababilities is returned.
 *
 ****************************************************************************/
static sockcaps_t espcp_usrsock_sockif_sockcaps(FAR struct socket *psock)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_sockif_sockcaps called.\n", _thisFile, __LINE__);
  return(-1);

  // return SOCKCAP_NONBLOCKING;
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_addref
 *
 * Description:
 *   Increment the reference count on the underlying connection structure.
 *
 * Input Parameters:
 *   psock - Socket structure of the socket whose reference count will be
 *           incremented.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void espcp_usrsock_sockif_addref(FAR struct socket *psock)
{
  // FAR struct usrsock_conn_s *conn;

  syslog(LOG_CRIT, "%s@%d espcp_usrsock_sockif_addref called.\n", _thisFile, __LINE__);

  // DEBUGASSERT(psock != NULL && psock->s_conn != NULL);

  // conn = psock->s_conn;
  // DEBUGASSERT(conn->crefs > 0 && conn->crefs < 255);
  // conn->crefs++;
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_send
 *
 * Description:
 *   The espcp_usrsock_sockif_send() call may be used only when the socket is in
 *   a connected state  (so that the intended recipient is known).
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *   buf      Data to send
 *   len      Length of data to send
 *   flags    Send flags (ignored)
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On  error, a negated
 *   errno value is returned (see send() for the list of appropriate error
 *   values.
 *
 ****************************************************************************/
static ssize_t espcp_usrsock_sockif_send(FAR struct socket *psock,
                                   FAR const void *buf,
                                   size_t len, int flags)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_sockif_send called.\n", _thisFile, __LINE__);
  return(-1);

  // return espcp_usrsock_sendto(psock, buf, len, flags, NULL, 0);
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_close
 *
 * Description:
 *   Performs the close operation on an USRSOCK socket instance
 *
 * Input Parameters:
 *   psock   Socket instance
 *
 * Returned Value:
 *   0 on success; -1 on error with errno set appropriately.
 *
 * Assumptions:
 *
 ****************************************************************************/
static int espcp_usrsock_sockif_close(FAR struct socket *psock)
{
  // FAR struct usrsock_conn_s *conn = psock->s_conn;
  // int ret;

  syslog(LOG_CRIT, "%s@%d espcp_usrsock_sockif_close called.\n", _thisFile, __LINE__);
  return(-1);

  /* Perform some pre-close operations for the USRSOCK socket type. */

  /* Is this the last reference to the connection structure (there
   * could be more if the socket was dup'ed).
   */

  // if (conn->crefs <= 1)
  //   {
  //     /* Yes... inform user-space daemon of socket close. */

  //     ret = espcp_usrsock_close(conn);

  //     /* Free the connection structure */

  //     conn->crefs = 0;
  //     espcp_usrsock_free(psock->s_conn);

  //     if (ret < 0)
  //       {
  //         /* Return with error code, but free resources. */

  //         nerr("ERROR: espcp_usrsock_close failed: %d\n", ret);
  //         return ret;
  //       }
  //   }
  // else
  //   {
  //     /* No.. Just decrement the reference count */

  //     conn->crefs--;
  //   }

  // return OK;
}

/****************************************************************************
 * Name:  espcp_usrsock_accept
 *
 * Description:
 *   The usrsock_sockif_accept function is used with connection-based socket
 *   types (SOCK_STREAM, SOCK_SEQPACKET and SOCK_RDM). It extracts the first
 *   connection request on the queue of pending connections, creates a new
 *   connected socket with mostly the same properties as 'sockfd', and
 *   allocates a new socket descriptor for the socket, which is returned. The
 *   newly created socket is no longer in the listening state. The original
 *   socket 'sockfd' is unaffected by this call.  Per file descriptor flags
 *   are not inherited across an inet_accept.
 *
 *   The 'sockfd' argument is a socket descriptor that has been created with
 *   socket(), bound to a local address with bind(), and is listening for
 *   connections after a call to listen().
 *
 *   On return, the 'addr' structure is filled in with the address of the
 *   connecting entity. The 'addrlen' argument initially contains the size
 *   of the structure pointed to by 'addr'; on return it will contain the
 *   actual length of the address returned.
 *
 *   If no pending connections are present on the queue, and the socket is
 *   not marked as non-blocking, inet_accept blocks the caller until a
 *   connection is present. If the socket is marked non-blocking and no
 *   pending connections are present on the queue, inet_accept returns
 *   EAGAIN.
 *
 * Parameters:
 *   psock    Reference to the listening socket structure
 *   addr     Receives the address of the connecting client
 *   addrlen  Input: allocated size of 'addr', Return: returned size of 'addr'
 *   newsock  Location to return the accepted socket information.
 *
 * Returned Value:
 *   Returns 0 (OK) on success.  On failure, it returns a negated errno
 *   value.  See accept() for a description of the appropriate error value.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/
int  espcp_usrsock_accept(FAR struct socket *psock, FAR struct sockaddr *addr,
                   FAR socklen_t *addrlen, FAR struct socket *newsock)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_accept called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_bind
 *
 * Description:
 *   usrsock_bind() gives the socket 'conn' the local address 'addr'. 'addr'
 *   is 'addrlen' bytes long. Traditionally, this is called "assigning a name
 *   to a socket." When a socket is created with socket, it exists in a name
 *   space (address family) but has no name assigned.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 *   addr     Socket local address
 *   addrlen  Length of 'addr'
 *
 * Returned Value:
 *   0 on success; -1 on error with errno set appropriately
 *
 *   EACCES
 *     The address is protected, and the user is not the superuser.
 *   EADDRINUSE
 *     The given address is already in use.
 *   EINVAL
 *     The socket is already bound to an address.
 *   ENOTSOCK
 *     psock is a descriptor for a file, not a socket.
 *
 * Assumptions:
 *
 ****************************************************************************/
int espcp_usrsock_bind(FAR struct socket *psock,
                 FAR const struct sockaddr *addr,
                 socklen_t addrlen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_bind called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_close
 *
 * Description:
 *
 ****************************************************************************/
int espcp_usrsock_close(FAR struct usrsock_conn_s *conn)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_close called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_connect
 *
 * Description:
 *   Perform a usrsock connection
 *
 * Input Parameters:
 *   psock - A reference to the socket structure of the socket to be connected
 *   addr    The address of the remote server to connect to
 *   addrlen Length of address buffer
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/
int espcp_usrsock_connect(FAR struct socket *psock,
                    FAR const struct sockaddr *addr, socklen_t addrlen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_connect called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_getpeername
 *
 * Description:
 *   The getpeername() function retrieves the remote-connected name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Parameters:
 *   conn     usrsock socket connection structure
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 ****************************************************************************/
int espcp_usrsock_getpeername(FAR struct socket *psock,
                        FAR struct sockaddr *addr, FAR socklen_t *addrlen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_getpeername called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_getsockname
 *
 * Description:
 *   The getsockname() function retrieves the locally-bound name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 ****************************************************************************/
int espcp_usrsock_getsockname(FAR struct socket *psock,
                        FAR struct sockaddr *addr, FAR socklen_t *addrlen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_getsockname called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_getsockopt
 *
 * Description:
 *   getsockopt() retrieve thse value for the option specified by the
 *   'option' argument for the socket specified by the 'psock' argument. If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be silently
 *   truncated. Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the'value'.
 *
 *   The 'level' argument specifies the protocol level of the option. To
 *   retrieve options at the socket level, specify the level argument as
 *   SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 *
 * Input Parameters:
 *   conn      usrsock socket connection structure
 *   level     Protocol level to set the option
 *   option    identifies the option to get
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 ****************************************************************************/
int espcp_usrsock_getsockopt(FAR struct socket *psock, int level, int option,
                       FAR void *value, FAR socklen_t *value_len)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_getsockopt called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_ioctl
 *
 * Description:
 *   The usrsock_ioctl() function performs network device specific operations.
 *
 * Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure
 *   cmd      The ioctl command
 *   arg      The argument of the ioctl cmd
 *
 ****************************************************************************/
int espcp_usrsock_ioctl(FAR struct socket *psock, int cmd, FAR void *arg,
                  size_t arglen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_ioctl called.\n", _thisFile, __LINE__);
  return(0);
}

/****************************************************************************
 * Name: espcp_usrsock_listen
 *
 * Description:
 *   To accept connections, a socket is first created with psock_socket(), a
 *   willingness to accept incoming connections and a queue limit for
 *   incoming connections are specified with psock_listen(), and then the
 *   connections are accepted with psock_accept().  For the case of AFINET
 *   and AFINET6 sockets, psock_listen() calls this function.  The
 *   psock_listen() call applies only to sockets of type SOCK_STREAM or
 *   SOCK_SEQPACKET.
 *
 * Parameters:
 *   psock    Reference to an internal, bound socket structure.
 *   backlog  The maximum length the queue of pending connections may grow.
 *            If a connection request arrives with the queue full, the client
 *            may receive an error with an indication of ECONNREFUSED or,
 *            if the underlying protocol supports retransmission, the request
 *            may be ignored so that retries succeed.
 *
 * Returned Value:
 *   On success, zero is returned. On error, a negated errno value is
 *   returned.  See list() for the set of appropriate error values.
 *
 ****************************************************************************/
int espcp_usrsock_listen(FAR struct socket *psock, int backlog)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_listen called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_poll
 *
 * Description:
 *   The standard poll() operation redirects operations on socket descriptors
 *   to this function.
 *
 * Input Parameters:
 *   psock - An instance of the internal socket structure.
 *   fds   - The structure describing the events to be monitored.
 *   setup - true: Setup up the poll; false: Teardown the poll
 *
 * Returned Value:
 *  0: Success; Negated errno on failure
 *
 ****************************************************************************/
int espcp_usrsock_poll(FAR struct socket *psock, FAR struct pollfd *fds, bool setup)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_poll called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_recvfrom
 *
 * Description:
 *   recvfrom() receives messages from a socket, and may be used to receive
 *   data on a socket whether or not it is connection-oriented.
 *
 *   If from is not NULL, and the underlying protocol provides the source
 *   address, this source address is filled in. The argument fromlen
 *   initialized to the size of the buffer associated with from, and modified
 *   on return to indicate the actual size of the address stored there.
 *
 * Input Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure
 *   buf      Buffer to receive data
 *   len      Length of buffer
 *   flags    Receive flags (ignored)
 *   from     Address of source (may be NULL)
 *   fromlen  The length of the address structure
 *
 ****************************************************************************/
ssize_t espcp_usrsock_recvfrom(FAR struct socket *psock, FAR void *buf, size_t len,
                         int flags, FAR struct sockaddr *from,
                         FAR socklen_t *fromlen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_recvfrom called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_sendto
 *
 * Description:
 *   If sendto() is used on a connection-mode (SOCK_STREAM, SOCK_SEQPACKET)
 *   socket, the parameters to and 'tolen' are ignored (and the error EISCONN
 *   may be returned when they are not NULL and 0), and the error ENOTCONN is
 *   returned when the socket was not actually connected.
 *
 * Input Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure
 *   buf      Data to send
 *   len      Length of data to send
 *   flags    Send flags (ignored)
 *   to       Address of recipient
 *   tolen    The length of the address structure
 *
 ****************************************************************************/
ssize_t espcp_usrsock_sendto(FAR struct socket *psock, FAR const void *buf,
                       size_t len, int flags, FAR const struct sockaddr *to,
                       socklen_t tolen)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_sendto called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_setsockopt
 *
 * Description:
 *   psock_setsockopt() sets the option specified by the 'option' argument,
 *   at the protocol level specified by the 'level' argument, to the value
 *   pointed to by the 'value' argument for the socket on the 'psock'
 *   argument.
 *
 *   The 'level' argument specifies the protocol level of the option. To set
 *   options at the socket level, specify the level argument as SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 *
 * Input Parameters:
 *   conn      usrsock socket connection structure
 *   level     Protocol level to set the option
 *   option    identifies the option to set
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 ****************************************************************************/
int espcp_usrsock_setsockopt(FAR struct socket *psock, int level, int option,
                       FAR const void *value, FAR socklen_t value_len)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_setsockopt called.\n", _thisFile, __LINE__);
  return(-1);
}

/****************************************************************************
 * Name: espcp_usrsock_socket
 *
 * Description:
 *   socket() creates an endpoint for communication and returns a socket
 *   structure.
 *
 * Input Parameters:
 *   domain   - (see sys/socket.h)
 *   type     - (see sys/socket.h)
 *   protocol - (see sys/socket.h)
 *   psock    - A pointer to a user allocated socket structure to be
 *              initialized.
 *
 * Returned Value:
 *   0 on success; negative error-code on error
 *
 *   EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 *   EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 *   EINVAL
 *     Unknown protocol, or protocol family not available.
 *   EMFILE
 *     Process file table overflow.
 *   ENFILE
 *     The system limit on the total number of open files has been reached.
 *   ENOBUFS or ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 *   EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 *
 * Assumptions:
 *
 ****************************************************************************/
int espcp_usrsock_socket(int domain, int type, int protocol,
                   FAR struct socket *psock)
{
  syslog(LOG_CRIT, "%s@%d espcp_usrsock_socket called.\n", _thisFile, __LINE__);
  return(-1);
}
