/****************************************************************************
 * configs/stm32f777-zit6-meadow/src/hcom/hcom_common_utils.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
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
#include "sqlite3.h"

// #include <assert.h>
// #include <string.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <sys/file.h>
// #include <sys/param.h>
// #include <unistd.h>
// #include <time.h>
// #include <stdbool.h>
// #include <errno.h>

#include "../hcom_common.h"

#define VIRTUAL_FILE_SYS_NAME "demo"

sqlite3_vfs *sqlite3_demovfs(void);
static int execute_sqlite_database(void);

//==============================================================
int hcom_sqlite_setup()
{
  syslog(1, "SQLite test:Entered hcom_sqlite_startup()\n");
  return OK;
}

// Callable from CLI
void hcom_exec_rqst_testing_developer_1(uint32_t userData)
{
  syslog(1, "SQLite test:Entered hcom_exec_rqst_testing_developer_1 switch\n");
  switch(userData)
  {
    case 1:
    execute_sqlite_database();
    break;
    
    case 2:
    break;

    default:
    break;
  }
}

void hcom_exec_rqst_testing_developer_2(uint32_t userData)
{
  return;
}

void hcom_exec_rqst_testing_developer_3(uint32_t userData)
{
  return;
}

void hcom_exec_rqst_testing_developer_4(uint32_t userData)
{
  return;
}

/****************************************************
Get first and last names from a tab-delimited file.
Then insert them into SQLlite database.
****************************************************/

#define DATABASE_NAME "/meadow0/Test1.db"
#define FILE_OF_NAMES "/meadow0/TabDelimited200Names.txt"
#define CREATE_TABLE "CREATE TABLE IF NOT EXISTS names () \
                      (nameid integer, \
                      first_name text not null, \
                      last_name text not null)"
#define BUFFER_SIZE 256

// Called from cli for testing
int execute_sqlite_database()
{
  sqlite3 *_sqlite_db;
  int ret;
    
  syslog(1, "SQLite test:Entered execute_sqlite_database\n");

  // Must register the vfs since we cannot use one of the built-in ones
  // To do this we ask the vfs for a pointer that represents itself.

  // For now we'll try to use the demo vfs (at least it builds)
  sqlite3_vfs *pVfs = sqlite3_demovfs();
  if(pVfs == NULL)
  {
    syslog(1, "ERROR: sqlite3_demovfs, error%d (%s)\n",
          sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "SQLite test:Step #1\n");

  // Then we register the vfs with sqlite
  ret = sqlite3_vfs_register(pVfs, 1);
  if(ret)
  {
    syslog(1, "ERROR: sqlite3_vfs_register, error%d (%s)\n",
          sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "SQLite test:Step #2\n");

  // Next we attempt to find it...??
  // This is really not needed because we already have what we
  // need, but for learning it seems a reasonable thing to do
  sqlite3_vfs *foundVfs = sqlite3_vfs_find(VIRTUAL_FILE_SYS_NAME);
  if(foundVfs == NULL)
  {
    syslog(1, "Cannot find vfs:%s, error%d (%s)\n",
          VIRTUAL_FILE_SYS_NAME, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "SQLite test:Step #3 - opening database\n");

  // Now we can use the vfs to open the database connection
  // https://sqlite.org/c3ref/open.html
  ret = sqlite3_open_v2(DATABASE_NAME,
          &_sqlite_db,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
          VIRTUAL_FILE_SYS_NAME);
  if(ret)
  {
    syslog(1, "Cannot open database:%s, error%d (%s)\n",
          DATABASE_NAME, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }

  syslog(1, "SQLite test:Step #4 - creating database\n");
  char *errMsg;
  ret = sqlite3_exec(_sqlite_db, CREATE_TABLE, NULL, NULL, &errMsg);
  if(ret)
  {
    syslog(1, "sqlite3_exec:'%s', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }

  syslog(1, "SQLite test:Step #5 - opening names file\n");
  FILE *pFile = fopen (FILE_OF_NAMES, "r");
  if(pFile == NULL)
  {
    syslog(1, "fopen:'%s', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }

  syslog(1, "SQLite test:Step #6 - filling names database\n");
  int n;
  char sInputBuf[BUFFER_SIZE];
  char *sFirstName;
  char *sLastName;
  char sSQL[256];
  char *pChar;

  while (!feof(pFile))
  {
    pChar = fgets (sInputBuf, BUFFER_SIZE, pFile);
    if(pChar == NULL)
    {
      syslog(1, "fgets returned NULL for %s\n", DATABASE_NAME);
      return -1;
    }

    sFirstName = strtok (sInputBuf, "\t");
    sLastName = strtok (NULL, "\t"); 

    sprintf(sSQL, "INSERT INTO Names VALUES (NULL, '%s', '%s', )", sFirstName, sLastName);
    syslog(1, "SQLite test:Step #7 - sqlite cmd '%s'\n", sSQL);
    ret = sqlite3_exec(_sqlite_db, sSQL, NULL, NULL, &errMsg);
    if(ret)
    {
      syslog(1, "sqlite3_exec INSERT into '%s', errMsg:%s, error:%d (%s)\n",
            DATABASE_NAME, errMsg, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
      return -1;
    }

    n++;
  }

  syslog(1, "SQLite test:Step #8 - closing file and database\n");
  fclose (pFile);
  sqlite3_close(_sqlite_db);
  syslog(1, "SQLite test:Step #9 - SQLite test exit\n");

  return OK;
}

SQLITE_API int sqlite3_os_init(void)
{
  syslog(1, "ENTERED sqlite3_os_init\n");
  return SQLITE_OK;
}

SQLITE_API int sqlite3_os_end(void)
{
  syslog(1, "ENTERED sqlite3_os_end\n");
  return SQLITE_OK;
}
