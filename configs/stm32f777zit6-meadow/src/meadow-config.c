/****************************************************************************
 * nuttx\configs\stm32f777zit6-meadow\src\meadow-config.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <nuttx/mm/mm.h>
#include <meadow/hcom_shared_common.h>
#include <fcntl.h>

#include "meadow-config.h"
#include "meadow-config-internal.h"

// This option is not needed at this time
#define MEADOW_CONFIG_USE_INI_PARSE_STRING 0
#define MEADOW_CONFIG_MAX_BUFFER_SPACE 256
/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int callbackChkSectionKey(void *user, const char *section, const char *key, const char *value);

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static bool isInitialized = false;
static struct host_com_cir_buffer_s *_config_cbuf;

typedef struct
{
  const char *cfgSectionName;
  const char *cfgKeyName;
  char *cfgValueBuf;
  int cfgReturnBufLen;
  int cfgNeededLength;
} meadow_config_find_data;

//=====================================================
// Not a lot to configure
int initialize_meadow_config(void)
{
  // Create a circular buffer to manage configuration file
  _config_cbuf = (struct host_com_cir_buffer_s *)malloc(sizeof(struct host_com_cir_buffer_s));
  if (_config_cbuf == NULL)
  {
    syslog(LOG_ERR, "%s@%d-cir buf alloc\n", thisFile, __LINE__);
    return -1;
  }

  // Allocate a buffer for the data
  int result = hcom_cirbuf_init(_config_cbuf, MEADOW_CONFIG_MAX_BUFFER_SPACE, 0x0a); // 0x0a line feed

  if (result == HCOM_CIR_BUF_INIT_FAILED)
  {
    syslog(LOG_ERR, "%s@%d-cirbuf_init\n", thisFile, __LINE__);
    return -1;
  }
  return OK;
}

//=====================================================
// 0 = success, returnValueBuf holds the value
// 1-n Configuration file parsing error and the return value is the line number
// -1 - n specific errors defined in meadow/hcom_shared_common.h
int meadow_config_find_value_from_key(const char *sectionName, const char *keyName,
                                      char returnValueBuf[], int returnBufLen)
{
  int ret;
  meadow_config_find_data find_data;

  if (!isInitialized)
  {
    ret = initialize_meadow_config();
    if (ret < 0)
    {
      return ret;
    }

    isInitialized = true;
  }

  // Prep request
  find_data.cfgSectionName = sectionName;
  find_data.cfgKeyName = keyName;
  find_data.cfgValueBuf = returnValueBuf;
  find_data.cfgReturnBufLen = returnBufLen;
  find_data.cfgNeededLength = 0;

  syslog(1, "---> Calling ini_parse, Section:%s, Key:%s, ValBuf:%p, BufLen:%d\n",
         find_data.cfgSectionName, find_data.cfgKeyName,
         find_data.cfgValueBuf, find_data.cfgReturnBufLen);
  usleep(20 * 1000);

  syslog(1, "---> In ini_parse_stream() INI_USE_STACK:%d\n", INI_USE_STACK);

  ret = ini_parse(MEADOW_CONFIG_FILE_FULL_NAME, callbackChkSectionKey, &find_data);

  if (find_data.cfgNeededLength)
    ret = MEADOW_CONFIG_ERROR_DEST_BUF_TOO_SMALL;

  if (ret == OK)
  {
    syslog(1, "SUCCESS returning a value of:'%s' from section:%s and key:%s\n",
        find_data.cfgValueBuf, find_data.cfgSectionName, find_data.cfgKeyName);
    usleep(20 * 1000);
    return ret;
  }
  
  if (ret == MEADOW_CONFIG_ERROR_NO_KEY_FOUND)
    syslog(1, "No matching key found:%d\n", ret);
  else if (ret == MEADOW_CONFIG_ERROR_CFG_FILE_OPEN)
    syslog(1, "File could not be opened:%d\n", ret);
  else if (ret == MEADOW_CONFIG_ERROR_DEST_BUF_TOO_SMALL)
    syslog(1, "Buffer too small, truncated results:%d\n", ret);
  else if (ret == MEADOW_CONFIG_ERROR_MEM_ALLOC_ERROR)
    syslog(1, "Memory allocation error:%d\n", ret);
  else if (ret == MEADOW_CONFIG_ERROR_CFG_LINE_TOO_LONG)
    syslog(1, "Line within file too long:%d\n", ret);
  else if (ret == MEADOW_CONFIG_ERROR_CFG_FILE_READ_ERR)
    syslog(1, "File read error:%d\n", ret);
  else
    syslog(1, "Encountered a parsing error on line:%d\n", ret);

  usleep(20 * 1000);
  return ret;
}

//=====================================================
// Find the first/next line
int meadow_config_get_next_line(int filefd, uint8_t *line, size_t maxLine)
{
  int ret;
  int cirBufReturn;
  size_t actualLen;
  size_t totalAdded = 0;

  for (;;)
  {
    // The circular buffer sees a line as a packet
    cirBufReturn = hcom_cirbuf_get_next_packet(_config_cbuf, line,
                                               maxLine, &actualLen);
    if (cirBufReturn == HCOM_CIR_BUF_GET_FOUND_MSG)
    {
      // Strip off 0x0d & 0x0a at the end and shorten length accordingly
      if(line[actualLen - 1] == 0x0a || line[actualLen - 1] == 0x0d)
        actualLen--;
      if(line[actualLen - 1] == 0x0a || line[actualLen - 1] == 0x0d)
        actualLen--;
      if(actualLen == 0)
      {
        syslog(1, "---> Blank Line\n");
        continue;
      }
      line[actualLen] = '\0';
      syslog(1, "---> Normal path, cirbuf found:'%s'\n", line);
      return actualLen;
    }

    if (cirBufReturn == HCOM_CIR_BUF_GET_DEST_NO_ROOM)
    {
      // Line too big to fit in line. this is an error
      // The field actualLen contains the needed size of buffer
      return MEADOW_CONFIG_ERROR_CFG_LINE_TOO_LONG;
    }

    if (cirBufReturn == HCOM_CIR_BUF_GET_NONE_FOUND)
    {
      // Need to read from file and add to cirbuf
      size_t roomInCir;
      size_t readSize;
      uint8_t tempBuf[64];
      ssize_t bytesRead;

      while ((roomInCir = hcom_cirbuf_avail_space(_config_cbuf)) > 0)
      {
        if (roomInCir == 0)
        {
          syslog(1, "---> This should never happen\n");
          break; // cirbuf totally full
        }
        else if (roomInCir < 64)
          readSize = roomInCir;
        else
          readSize = 64;

        // return 1-n=number read, 0=EOF, -1-n error
        bytesRead = read(filefd, tempBuf, readSize);
        if (bytesRead < 0)
        {
          // error
          return MEADOW_CONFIG_ERROR_CFG_FILE_READ_ERR;
          //return -errno;
        }

        if (bytesRead == 0)
        {
          // End of file reached. Exit this loop and read
          break;
        }

        // Successful read
        ret = hcom_cirbuf_add_bytes(_config_cbuf, tempBuf, bytesRead);
        DEBUGASSERT(ret == HCOM_CIR_BUF_ADD_SUCCESS);
        DEBUGASSERT(ret != HCOM_CIR_BUF_ADD_WONT_FIT);
        DEBUGASSERT(ret != HCOM_CIR_BUF_ADD_BAD_ARG);

        totalAdded += bytesRead;
      } // while()

      // No characters available, reached end of file etc.
      if (totalAdded == 0)
      {
        syslog(1, "---> totalAdded == 0 time to exit loop, no key\n");
        return MEADOW_CONFIG_ERROR_NO_KEY_FOUND;
      }

      if (bytesRead == 0)
      {
        // EOF reached, no need to read the file any more but
        // there must be buffered characters since totalAdded > 0
        syslog(1, "---> Reached EOF, but there are charcters\n");
      }

      if (roomInCir == 0)
      {
        // Totally full cirbuf. So, get next line
        syslog(1, "---> CirBuf is totally full\n");
      }

      // This can be reached if reach EOF and we didn't fill the cirbuf
      syslog(1, "---> Can this location be reached? bytesRead:%d, roomInCir:%d\n",
             bytesRead, roomInCir);
    } // none found

    // 'continue' by default.
  } // for(;;)
}

//=====================================================
// CAllback for each section/key pair located. This function looks
// for the matching one
int callbackChkSectionKey(void *user, const char *section, const char *key,
                          const char *value)
{
  meadow_config_find_data *find_data = (meadow_config_find_data *)user;

  // Ignore section?
  if (find_data->cfgSectionName == NULL)
  {
    if (strcmp(find_data->cfgKeyName, key) != 0)
      return 1; // Tell parser to keep looking
  }
  else
  {
    if ((strcmp(find_data->cfgSectionName, section) != 0) ||
        (strcmp(find_data->cfgKeyName, key) != 0))
      return 1; // Tell parser to keep looking
  }

  // Matching entry. Insure it fit in the provided buffer?
  size_t neededLength = strlen(value) + 1;
  if (neededLength > find_data->cfgReturnBufLen)
  {
    find_data->cfgNeededLength = neededLength;
    neededLength = find_data->cfgReturnBufLen - 1;
  }

  strncpy(find_data->cfgValueBuf, value, neededLength);
  find_data->cfgValueBuf[neededLength] = '\0';
  return 0; // Tell parser to exit with success
}

// Original Code here:https://github.com/benhoyt/inih
// Cloned 6-Nov-2020
// Modifications made by Peter Moody, Wilderness Labs Inc.
// Start of original code vvvv
//--------------------------------------------------------------
/* inih -- simple .INI file parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2009-2020, Ben Hoyt

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:

https://github.com/benhoyt/inih

*/

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

// #include "ini.h"

#if !INI_USE_STACK
#if INI_CUSTOM_ALLOCATOR
#include <stddef.h>
void *ini_malloc(size_t size);
void ini_free(void *ptr);
void *ini_realloc(void *ptr, size_t size);
#else
#include <stdlib.h>
#define ini_malloc kmm_malloc
#define ini_free kmm_free
#define ini_realloc realloc
#endif
#endif

#define MAX_SECTION 50
#define MAX_NAME 50

#if MEADOW_CONFIG_USE_INI_PARSE_STRING > 0
/* Used by ini_parse_string() to keep track of string parsing state. */
typedef struct
{
  const char *ptr;
  size_t num_left;
} ini_parse_string_ctx;
#endif

/* Strip whitespace chars off end of given string, in place. Return s. */
static char *rstrip(char *s)
{
  char *p = s + strlen(s);
  while (p > s && isspace((unsigned char)(*--p)))
    *p = '\0';
  return s;
}

/* Return pointer to first non-whitespace char in given string. */
static char *lskip(const char *s)
{
  while (*s && isspace((unsigned char)(*s)))
    s++;
  return (char *)s;
}

/* Return pointer to first char (of chars) or inline comment in given string,
   or pointer to NUL at end of string if neither found. Inline comment must
   be prefixed by a whitespace character to register as a comment. */
static char *find_chars_or_comment(const char *s, const char *chars)
{
#if INI_ALLOW_INLINE_COMMENTS
  int was_space = 0;
  while (*s && (!chars || !strchr(chars, *s)) &&
         !(was_space && strchr(INI_INLINE_COMMENT_PREFIXES, *s)))
  {
    was_space = isspace((unsigned char)(*s));
    s++;
  }
#else
  while (*s && (!chars || !strchr(chars, *s)))
  {
    s++;
  }
#endif
  return (char *)s;
}

/* Similar to strncpy, but ensures dest (size bytes) is
   NUL-terminated, and doesn't pad with NULs. */
static char *strncpy0(char *dest, const char *src, size_t size)
{
  /* Could use strncpy internally, but it causes gcc warnings (see issue #91) */
  size_t i;
  for (i = 0; i < size - 1 && src[i]; i++)
    dest[i] = src[i];
  dest[i] = '\0';
  return dest;
}

/* See documentation in header file. */
int ini_parse_stream(int filefd, ini_handler handler, void *user)
{
  syslog(1, "---x Entered ini_parse_stream() 1\n");
  usleep(20 * 1000);

  /* Uses a fair bit of stack (use heap instead if you need to) */
#if INI_USE_STACK
  char line[INI_MAX_LINE];
  int max_line = INI_MAX_LINE;
#else
  char *line;
  size_t max_line = INI_INITIAL_ALLOC;
#endif
#if INI_ALLOW_REALLOC && !INI_USE_STACK
  char *new_line;
  size_t offset;
#endif
  char section[MAX_SECTION] = "";
  char prev_name[MAX_NAME] = "";

  char *start;
  char *end;
  char *name;
  char *value;
  int lineno = 0;
  int error = 0;

#if !INI_USE_STACK

  syslog(1, "---x In ini_parse_stream() alloc line 2\n");
  usleep(20 * 1000);
  line = (char *)ini_malloc(INI_INITIAL_ALLOC);
  if (!line)
  {
    return MEADOW_CONFIG_ERROR_MEM_ALLOC_ERROR;
  }
#endif
  syslog(1, "---x In ini_parse_stream() alloc SUCCESS 3\n");
  usleep(20 * 1000);

#if INI_HANDLER_LINENO
#define HANDLER(u, s, n, v) handler(u, s, n, v, lineno)
#else
#define HANDLER(u, s, n, v) handler(u, s, n, v)
#endif

  syslog(1, "---x In ini_parse_stream() Entering while loop 4\n");
  usleep(20 * 1000);

  /* Scan through stream line by line */
  int iniLoopCount = 0;
  int readReturn;

  while ((readReturn = meadow_config_get_next_line(filefd,
                            (uint8_t *)line, (size_t)max_line)) > 0)
  {
    syslog(1, "---x In ini_parse_stream-within while loop:%d, %s\n",
           ++iniLoopCount, line);
    usleep(20 * 1000);

#if INI_ALLOW_REALLOC && !INI_USE_STACK
    offset = strlen(line);
    while (offset == max_line - 1 && line[offset - 1] != '\n')
    {
      max_line *= 2;
      if (max_line > INI_MAX_LINE)
        max_line = INI_MAX_LINE;
      new_line = ini_realloc(line, max_line);
      if (!new_line)
      {
        ini_free(line);
        return MEADOW_CONFIG_ERROR_MEM_ALLOC_ERROR;
      }
      line = new_line;
      if (reader(line + offset, (int)(max_line - offset), stream) == NULL)
        break;
      if (max_line >= INI_MAX_LINE)
        break;
      offset += strlen(line + offset);
    }
#endif

    lineno++;

    start = line;
#if INI_ALLOW_BOM
    if (lineno == 1 && (unsigned char)start[0] == 0xEF &&
                       (unsigned char)start[1] == 0xBB &&
                       (unsigned char)start[2] == 0xBF)
    {
      start += 3;
    }
#endif
    start = lskip(rstrip(start));

    if (strchr(INI_START_COMMENT_PREFIXES, *start))
    {
      /* Start-of-line comment */
    }
#if INI_ALLOW_MULTILINE
    else if (*prev_name && *start && start > line)
    {
      /* Non-blank line with leading whitespace, treat as continuation
               of previous name's value (as per Python configparser). */
      if (!HANDLER(user, section, prev_name, start) && !error)
        break; // done
    }
#endif
    else if (*start == '[')
    {
      /* A "[section]" line */
      end = find_chars_or_comment(start + 1, "]");
      if (*end == ']')
      {
        *end = '\0';
        strncpy0(section, start + 1, sizeof(section));
        *prev_name = '\0';
#if INI_CALL_HANDLER_ON_NEW_SECTION
        if (!HANDLER(user, section, NULL, NULL) && !error)
          break; // done
#endif
      }
      else if (!error)
      {
        /* No ']' found on section line */
        error = lineno;
      }
    }
    else if (*start)
    {
      /* Not a comment, must be a name[=:]value pair */
      end = find_chars_or_comment(start, "=:");
      if (*end == '=' || *end == ':')
      {
        *end = '\0';
        name = rstrip(start);
        value = end + 1;
#if INI_ALLOW_INLINE_COMMENTS
        end = find_chars_or_comment(value, NULL);
        if (*end)
          *end = '\0';
#endif
        value = lskip(value);
        rstrip(value);

        /* Valid name[=:]value pair found, call handler */
        strncpy0(prev_name, name, sizeof(prev_name));
        if (!HANDLER(user, section, name, value) && !error)
          break; // done
      }
      else if (!error)
      {
        /* No '=' or ':' found on name[=:]value line */
#if INI_ALLOW_NO_VALUE
        *end = '\0';
        name = rstrip(start);
        if (!HANDLER(user, section, name, NULL) && !error)
          break; // done
#else
        error = lineno;
#endif
      }
    }

#if INI_STOP_ON_FIRST_ERROR
    if (error)
      break;
#endif
  } // end while

  // Reached EOF and no match found?
  if (readReturn == 0)
    return MEADOW_CONFIG_ERROR_NO_KEY_FOUND;

  if (readReturn < 0)
  {
    syslog(1, "---x In ini_parse_stream() left while loop after:%d\n", iniLoopCount);
  }

  syslog(1, "---x In ini_parse_stream() left while loop after:%d\n", iniLoopCount);
  usleep(20 * 1000);

#if !INI_USE_STACK
  syslog(1, "---x In ini_parse_stream() about to free 6\n");
  ini_free(line);
#endif

  syslog(1, "---x Exiting ini_parse_stream() left while loop\n");
  usleep(20 * 1000);
  return error; // Actually line number
}

/* See documentation in header file. */
int ini_parse_file(int filefd, ini_handler handler, void *user)
{
  return ini_parse_stream(filefd, handler, user);
}

/* See documentation in header file. */
int ini_parse(const char *filename, ini_handler handler, void *user)
{
  int error;
  int filefd;

  filefd = open(filename, O_RDONLY);
  if (filefd == -1)
  {
    syslog(LOG_ERR, "%s@%d-file %s not found, errno:%d.", thisFile, __LINE__,
           filename, errno);
    return -1;
  }

  // Do the work
  error = ini_parse_file(filefd, handler, user);
  close(filefd);

  syslog(1, "---x Exiting ini_parse() returning:%d\n", error);
  usleep(20 * 1000);
  return error;
}

#if MEADOW_CONFIG_USE_INI_PARSE_STRING > 0
/* An ini_reader function to read the next line from a string buffer. This
   is the fgets() equivalent used by ini_parse_string(). */
static char *ini_reader_string(char *str, int num, void *stream)
{
  ini_parse_string_ctx *ctx = (ini_parse_string_ctx *)stream;
  const char *ctx_ptr = ctx->ptr;
  size_t ctx_num_left = ctx->num_left;
  char *strp = str;
  char c;

  if (ctx_num_left == 0 || num < 2)
    return NULL;

  while (num > 1 && ctx_num_left != 0)
  {
    c = *ctx_ptr++;
    ctx_num_left--;
    *strp++ = c;
    if (c == '\n')
      break;
    num--;
  }

  *strp = '\0';
  ctx->ptr = ctx_ptr;
  ctx->num_left = ctx_num_left;
  return str;
}

/* See documentation in header file. */
int ini_parse_string(const char *string, ini_handler handler, void *user)
{
  ini_parse_string_ctx ctx;

  ctx.ptr = string;
  ctx.num_left = strlen(string);
  return ini_parse_stream((ini_reader)ini_reader_string, &ctx, handler,
                          user);
}
#endif // #if MEADOW_CONFIG_USE_INI_PARSE_STRING > 0
