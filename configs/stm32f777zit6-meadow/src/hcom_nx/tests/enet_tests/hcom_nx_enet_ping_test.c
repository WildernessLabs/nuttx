/****************************************************************************
 * \apps\examples\hcom\tests\ethernet_tests\enet_ping_test.c
 *   Copyright (C) 2021 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 ****************************************************************************/

// Copied from the system/ping/ directory. Why? Because NSH Built-in apps
// are not generally available on a protected build. In fact this may not
// work and may need to be moved to NuttxLand.

/****************************************************************************
 * apps/netutils/ping/icmp_ping.c
 *
 *   Copyright (C) 2018 Pinecone Inc. All rights reserved.
 *   Author: Guiding Li<liguiding@pinecone.net>
 *
 * Extracted from logic originally written by:
 *
 *   Copyright (C) 2017-2018 Gregory Nutt. All rights reserved.
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
#include <meadow/hcom_shared_common.h>

// #if defined(HCOM_INCLUDE_EMBEDDED_ETHERNET_TESTS)
#if 1
/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#include <sys/socket.h>

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#if defined(CONFIG_LIBC_NETDB) && defined(CONFIG_NETDB_DNSCLIENT)
#  include <netdb.h>
#endif

#include <arpa/inet.h>

#include <nuttx/clock.h>
#include <nuttx/net/icmp.h>
#include <meadow/hcom_nuttx_shared.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ICMP_M_IOBUFFER_SIZE(x) (sizeof(struct icmp_hdr_s) + (x))

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* NOTE: This will not work in the kernel build where there will be a
 * separate instance of g_pingid in every process space.
 */

static uint16_t g_pingid = 0;

/****************************************************************************
 * Private Types
 ****************************************************************************/
// Copied from icmp_ping.h and modified
/* Positive number represent information */

#define ICMP_M_I_BEGIN       0   /* extra: not used      */
#define ICMP_M_I_ROUNDTRIP   1   /* extra: packet delay  */
#define ICMP_M_I_FINISH      2   /* extra: elapsed time  */

/* Negative odd number represent error(unrecoverable) */

#define ICMP_M_E_HOSTIP      -1  /* extra: not used      */
#define ICMP_M_E_MEMORY      -3  /* extra: not used      */
#define ICMP_M_E_SOCKET      -5  /* extra: error code    */
#define ICMP_M_E_SENDTO      -7  /* extra: error code    */
#define ICMP_M_E_SENDSMALL   -9  /* extra: sent bytes    */
#define ICMP_M_E_POLL        -11 /* extra: error code    */
#define ICMP_M_E_RECVFROM    -13 /* extra: error code    */
#define ICMP_M_E_RECVSMALL   -15 /* extra: recv bytes    */

/* Negative even number represent warning(recoverable) */

#define ICMP_M_W_TIMEOUT     -2  /* extra: timeout value */
#define ICMP_M_W_IDDIFF      -4  /* extra: recv id       */
#define ICMP_M_W_SEQNOBIG    -6  /* extra: recv seqno    */
#define ICMP_M_W_SEQNOSMALL  -8  /* extra: recv seqno    */
#define ICMP_M_W_RECVBIG     -10 /* extra: recv bytes    */
#define ICMP_M_W_DATADIFF    -12 /* extra: not used      */
#define ICMP_M_W_TYPE        -14 /* extra: recv type     */


struct ping_result_xs;

struct ping_info_xs
{
  FAR const char *hostname; /* Host name to ping */
  uint16_t count;           /* Number of pings requested */
  uint16_t datalen;         /* Number of bytes to be sent */
  uint16_t delay;           /* Deciseconds to delay between pings */
  uint16_t timeout;         /* Deciseconds to wait response before timeout */
  FAR void *priv;           /* Private context for callback */
  void (*callback)(FAR const struct ping_result_xs *result);
};

struct ping_result_xs
{
  int linenumb;
  int code;                 /* Notice code ICMP_M_I/E/W_XXX */
  int extra;                /* Extra information for code */
  struct in_addr dest;      /* Target address to ping */
  uint16_t nrequests;       /* Number of ICMP ECHO requests sent */
  uint16_t nreplies;        /* Number of matching ICMP ECHO replies received */
  uint16_t outsize;         /* Bytes(include ICMP header) to be sent */
  uint16_t id;              /* ICMP_M_ECHO id */
  uint16_t seqno;           /* ICMP_M_ECHO seqno */
  FAR const struct ping_info_xs *info;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ping_newid
 ****************************************************************************/

static inline uint16_t ping_newid(void)
{
  /* Revisit:  No thread safe */

  return ++g_pingid;
}

/****************************************************************************
 * Name: ping_gethostip
 *
 * Description:
 *   Call gethostbyname() to get the IP address associated with a hostname.
 *
 * Input Parameters
 *   hostname - The host name to use in the nslookup.
 *   ipv4addr - The location to return the IPv4 address.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int ping_gethostip(FAR const char *hostname, FAR struct in_addr *dest)
{
#if defined(CONFIG_LIBC_NETDB) && defined(CONFIG_NETDB_DNSCLIENT)
  /* Netdb DNS client support is enabled */

  FAR struct hostent *he;

  he = gethostbyname(hostname);
  if (he == NULL)
    {
      return -ENOENT;
    }
  else if (he->h_addrtype == AF_INET)
    {
       memcpy(dest, he->h_addr, sizeof(in_addr_t));
    }
  else
    {
      return -ENOEXEC;
    }

  return OK;

#else /* CONFIG_LIBC_NETDB */

  /* No host name support */
  /* Convert strings to numeric IPv6 address */

  int ret = inet_pton(AF_INET, hostname, dest);

  /* The inet_pton() function returns 1 if the conversion succeeds. It will
   * return 0 if the input is not a valid IPv4 dotted-decimal string or -1
   * with errno set to EAFNOSUPPORT if the address family argument is
   * unsupported.
   */

  return (ret > 0) ? OK : ERROR;

#endif /* CONFIG_LIBC_NETDB */
}

/****************************************************************************
 * Name: icmp_callback_m
 ****************************************************************************/

static void icmp_callback_m(FAR struct ping_result_xs *result, int code, int extra, int linenumb)
{
  result->code = code;
  result->extra = extra;
  result->linenumb = linenumb;
  
  result->info->callback(result);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: icmp_ping_m
 ****************************************************************************/

void icmp_ping_m(FAR const struct ping_info_xs *info)
{
  struct ping_result_xs result;
  struct sockaddr_in destaddr;
  struct sockaddr_in fromaddr;
  struct icmp_hdr_s outhdr;
  FAR struct icmp_hdr_s *inhdr;
  struct pollfd recvfd;
  FAR uint8_t *iobuffer;
  FAR uint8_t *ptr;
  int32_t elapsed;
  clock_t kickoff;
  clock_t start;
  socklen_t addrlen;
  ssize_t nsent;
  ssize_t nrecvd;
  bool retry;
  int sockfd;
  int ret;
  int ch;
  int i;

  /* Initialize result structure */

  memset(&result, 0, sizeof(result));
  result.info = info;
  result.id = ping_newid();
  result.outsize = ICMP_M_IOBUFFER_SIZE(info->datalen);
  if (ping_gethostip(info->hostname, &result.dest) < 0)
    {
      icmp_callback_m(&result, ICMP_M_E_HOSTIP, 0, __LINE__);
      return;
    }
    // result.dest is "backward"
  syslog(2, "==>The host name is:'%s', result.dest:0x%08x\n", info->hostname, result.dest);

  /* Allocate memory to hold ping buffer */
 
  iobuffer = (FAR uint8_t *)malloc(result.outsize);
  if (iobuffer == NULL)
    {
      icmp_callback_m(&result, ICMP_M_E_MEMORY, 0, __LINE__);
      return;
    }

  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if (sockfd < 0)
    {
      icmp_callback_m(&result, ICMP_M_E_SOCKET, errno, __LINE__);
      free(iobuffer);
      return;
    }

  kickoff = clock();

  memset(&destaddr, 0, sizeof(struct sockaddr_in));
  destaddr.sin_family      = AF_INET;
  destaddr.sin_port        = 0;
  destaddr.sin_addr.s_addr = result.dest.s_addr;

  memset(&outhdr, 0, sizeof(struct icmp_hdr_s));
  outhdr.type              = ICMP_ECHO_REQUEST;
  outhdr.id                = htons(result.id);
  outhdr.seqno             = htons(result.seqno);

  icmp_callback_m(&result, ICMP_M_I_BEGIN, 0, __LINE__);

  while (result.nrequests < info->count)
    {
      /* Copy the ICMP header into the I/O buffer */

      memcpy(iobuffer, &outhdr, sizeof(struct icmp_hdr_s));

     /* Add some easily verifiable payload data */

      ptr = &iobuffer[sizeof(struct icmp_hdr_s)];
      ch  = 0x20;

      for (i = 0; i < info->datalen; i++)
        {
          *ptr++ = ch;
          if (++ch > 0x7e)
            {
              ch = 0x20;
            }
        }

      start = clock();
      nsent = sendto(sockfd, iobuffer, result.outsize, 0,
                     (FAR struct sockaddr*)&destaddr,
                     sizeof(struct sockaddr_in));
      if (nsent < 0)
        {
          icmp_callback_m(&result, ICMP_M_E_SENDTO, errno, __LINE__);
          goto done;
        }
      else if (nsent != result.outsize)
        {
          icmp_callback_m(&result, ICMP_M_E_SENDSMALL, nsent, __LINE__);
          goto done;
        }

      result.nrequests++;

      elapsed = 0;
      do
        {
          retry           = false;

          recvfd.fd       = sockfd;
          recvfd.events   = POLLIN;
          recvfd.revents  = 0;

          ret = poll(&recvfd, 1, info->timeout - elapsed);
          if (ret < 0)
            {
              icmp_callback_m(&result, ICMP_M_E_POLL, errno, __LINE__);
              goto done;
            }
          else if (ret == 0)
            {
              icmp_callback_m(&result, ICMP_M_W_TIMEOUT, info->timeout, __LINE__);
              continue;
            }

          /* Get the ICMP response (ignoring the sender) */

          addrlen = sizeof(struct sockaddr_in);
          nrecvd  = recvfrom(sockfd, iobuffer, result.outsize, 0,
                             (FAR struct sockaddr *)&fromaddr, &addrlen);
          if (nrecvd < 0)
            {
              icmp_callback_m(&result, ICMP_M_E_RECVFROM, errno, __LINE__);
              goto done;
            }
          else if (nrecvd < sizeof(struct icmp_hdr_s))
            {
              icmp_callback_m(&result, ICMP_M_E_RECVSMALL, nrecvd, __LINE__);
             goto done;
            }

          elapsed = (unsigned int)TICK2MSEC(clock() - start);
          inhdr   = (FAR struct icmp_hdr_s *)iobuffer;

          if (inhdr->type == ICMP_ECHO_REPLY)
            {
              if (ntohs(inhdr->id) != result.id)
                {
                  icmp_callback_m(&result, ICMP_M_W_IDDIFF, ntohs(inhdr->id), __LINE__);
                  retry = true;
                }
              else if (ntohs(inhdr->seqno) > result.seqno)
                {
                  icmp_callback_m(&result, ICMP_M_W_SEQNOBIG, ntohs(inhdr->seqno), __LINE__);
                  retry = true;
                }
              else
                {
                  bool verified = true;
                  int32_t pktdelay = elapsed;

                  if (ntohs(inhdr->seqno) < result.seqno)
                    {
                      icmp_callback_m(&result, ICMP_M_W_SEQNOSMALL, ntohs(inhdr->seqno), __LINE__);
                      pktdelay += info->delay;
                      retry     = true;
                    }

                  icmp_callback_m(&result, ICMP_M_I_ROUNDTRIP, pktdelay, __LINE__);

                  /* Verify the payload data */

                  if (nrecvd != result.outsize)
                    {
                      icmp_callback_m(&result, ICMP_M_W_RECVBIG, nrecvd, __LINE__);
                      verified = false;
                    }
                  else
                    {
                      ptr = &iobuffer[sizeof(struct icmp_hdr_s)];
                      ch  = 0x20;

                      for (i = 0; i < info->datalen; i++, ptr++)
                        {
                          if (*ptr != ch)
                            {
                              icmp_callback_m(&result, ICMP_M_W_DATADIFF, 0, __LINE__);
                              verified = false;
                              break;
                            }

                          if (++ch > 0x7e)
                            {
                              ch = 0x20;
                            }
                        }
                    }

                  /* Only count the number of good replies */

                  if (verified)
                    {
                      result.nreplies++;
                    }
                }
            }
          else
            {
              icmp_callback_m(&result, ICMP_M_W_TYPE, inhdr->type, __LINE__);
            }
        }
      while (retry && info->delay > elapsed && info->timeout > elapsed);

      /* Wait if necessary to preserved the requested ping rate */

      elapsed = (unsigned int)TICK2MSEC(clock() - start);
      if (elapsed < info->delay)
        {
          struct timespec rqt;
          unsigned int remaining;
          unsigned int sec;
          unsigned int frac;  /* In deciseconds */

          remaining   = info->delay - elapsed;
          sec         = remaining / MSEC_PER_SEC;
          frac        = remaining - MSEC_PER_SEC * sec;

          rqt.tv_sec  = sec;
          rqt.tv_nsec = frac * NSEC_PER_MSEC;

          (void)nanosleep(&rqt, NULL);
        }

      outhdr.seqno = htons(++result.seqno);
    }

done:
  icmp_callback_m(&result, ICMP_M_I_FINISH, TICK2MSEC(clock() - kickoff), __LINE__);
  close(sockfd);
  free(iobuffer);
}

/****************************************************************************
 * From: apps/system/ping/ping.c
 *
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ICMP_M_PING_DATALEN  56

#define ICMP_M_NPINGS        10    /* Default number of pings */
#define ICMP_M_POLL_DELAY    1000  /* 1 second in milliseconds */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ping_result_m
 ****************************************************************************/

static void ping_result_m(FAR const struct ping_result_xs *result)
{
  switch (result->code)
    {
      case ICMP_M_E_HOSTIP:
        syslog(2, "ERROR: ping_gethostip(%s) failed @%d\n",
                result->info->hostname, result->linenumb);
        break;

      case ICMP_M_E_MEMORY:
        syslog(2, "ERROR: Failed to allocate memory @%d\n", result->linenumb);
        break;

      case ICMP_M_E_SOCKET:
        syslog(2, "ERROR: socket() failed: %d @%d\n", result->extra, result->linenumb);
        break;

      case ICMP_M_I_BEGIN:
        syslog(2, "PING %u.%u.%u.%u %u bytes of data @%d\n",
               (result->dest.s_addr      ) & 0xff,
               (result->dest.s_addr >> 8 ) & 0xff,
               (result->dest.s_addr >> 16) & 0xff,
               (result->dest.s_addr >> 24) & 0xff,
               result->info->datalen, result->linenumb);
        break;

      case ICMP_M_E_SENDTO:
        syslog(2, "ERROR: sendto failed at seqno %u: %d @%d\n",
                result->seqno, result->extra, result->linenumb);
        break;

      case ICMP_M_E_SENDSMALL:
        syslog(2, "ERROR: sendto returned %d, expected %u @%d\n",
                result->extra, result->outsize, result->linenumb);
        break;

      case ICMP_M_E_POLL:
        syslog(2, "ERROR: poll failed: %d @%d\n", result->extra, result->linenumb);
        break;

      case ICMP_M_W_TIMEOUT:
        printf("No response from %u.%u.%u.%u: icmp_seq=%u time=%d ms @%d\n",
               (result->dest.s_addr      ) & 0xff,
               (result->dest.s_addr >> 8 ) & 0xff,
               (result->dest.s_addr >> 16) & 0xff,
               (result->dest.s_addr >> 24) & 0xff,
               result->seqno, result->extra, result->linenumb);
        break;

      case ICMP_M_E_RECVFROM:
        syslog(2, "ERROR: recvfrom failed: %d @%d\n", result->extra, result->linenumb);
        break;

      case ICMP_M_E_RECVSMALL:
        syslog(2, "ERROR: short ICMP packet: %d @%d\n", result->extra, result->linenumb);
        break;

      case ICMP_M_W_IDDIFF:
        syslog(2,
                "WARNING: Ignoring ICMP reply with ID %d.  "
                "Expected %u @%d\n",
                result->extra, result->id, result->linenumb);
        break;

      case ICMP_M_W_SEQNOBIG:
        syslog(2,
                "WARNING: Ignoring ICMP reply to sequence %d.  "
                "Expected <= %u @%d\n",
                result->extra, result->seqno, result->linenumb);
        break;

      case ICMP_M_W_SEQNOSMALL:
        syslog(2, "WARNING: Received after timeout @%d\n", result->linenumb);
        break;

      case ICMP_M_I_ROUNDTRIP:
        syslog(2, "%u bytes from %u.%u.%u.%u: icmp_seq=%u time=%d ms @%d\n",
               result->info->datalen,
               (result->dest.s_addr      ) & 0xff,
               (result->dest.s_addr >> 8 ) & 0xff,
               (result->dest.s_addr >> 16) & 0xff,
               (result->dest.s_addr >> 24) & 0xff,
               result->seqno, result->extra, result->linenumb);
        break;

      case ICMP_M_W_RECVBIG:
        syslog(2,
                "WARNING: Ignoring ICMP reply with different payload "
                "size: %d vs %u @%d\n",
                result->extra, result->outsize, result->linenumb);
        break;

      case ICMP_M_W_DATADIFF:
        syslog(2, "WARNING: Echoed data corrupted @%d\n", result->linenumb);
        break;

      case ICMP_M_W_TYPE:
        syslog(2, "WARNING: ICMP packet with unknown type: %d @%d\n",
                result->extra, result->linenumb);
        break;

      case ICMP_M_I_FINISH:
        if (result->nrequests > 0)
          {
            unsigned int tmp;

            /* Calculate the percentage of lost packets */

            tmp = (100 * (result->nrequests - result->nreplies) +
                  (result->nrequests >> 1)) /
                   result->nrequests;

            syslog(2, "%u packets transmitted, %u received, %u%% packet loss, time %d ms @%d\n",
                   result->nrequests, result->nreplies, tmp, result->extra, result->linenumb);
          }
        break;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

// This is called via CLI to execute ping. The user data defines the options
int hcom_nx_ethernet_ping_test(struct hcom_nx_cmd_data *cmdData)
{
  struct ping_info_xs info;
  int userData = cmdData->userData;

syslog(2, "Entered hcom_nx_ethernet_ping_test(%d)\n", userData);

  info.count     = ICMP_M_NPINGS;
  info.datalen   = ICMP_M_PING_DATALEN;
  info.delay     = ICMP_M_POLL_DELAY;
  info.timeout   = ICMP_M_POLL_DELAY;
  info.callback  = ping_result_m;

  // We have 16 options
  DEBUGASSERT(userData < 0x10 && userData > 0);

  switch (userData)
  {
    case 0:
      info.hostname = "192.168.2.81";
    break;

    case 1:
      info.hostname = "google.com";
    break;

    default:
     return -1;
  }

  icmp_ping_m(&info);
  return EXIT_SUCCESS;
}

#endif //#if defined(HCOM_INCLUDE_EMBEDDED_ETHERNET_TESTS)
