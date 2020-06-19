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
static int execute_sqlite_database_experiment(void);

//==============================================================
int hcom_sqlite_setup()
{
  syslog(1, "SQLite test:Entered hcom_sqlite_startup()\n");
  return OK;
}

//==============================================================
// Callable from CLI
void hcom_exec_rqst_testing_developer_1(uint32_t userData)
{
  syslog(1, "SQLite test:Entered hcom_exec_rqst_testing_developer_1 switch\n");
  
  switch(userData)
  {
    case 1:
    execute_sqlite_database_experiment();
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

//=========================================================
#define BUFFER_SIZE 256

// Called from cli for testing
int execute_sqlite_database_experiment()
{
  sqlite3 *_sqlite_db;
  int ret;
  char *errMsg;
    
  syslog(1, "MGR-SQLite test:Started\n");

  // Must register the vfs since we cannot use one of the built-in ones
  // To do this we ask the vfs for a pointer that represents itself.

  syslog(1, "MGR-SQLite test:Step #1-register vfs\n");
  usleep(10 * 1000);
  // For now we'll try to use the demo vfs (at least it builds)
  sqlite3_vfs *pVfs = sqlite3_demovfs();
  if(pVfs == NULL)
  {
    syslog(1, "MGR-SQLite ERROR: sqlite3_demovfs, error%d (%s)\n",
          sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "MGR-SQLite test:Step #1-register vfs - success\n");
  syslog(1, "MGR-SQLite test:Step #2-find just registered self\n");
  usleep(10 * 1000);

  // Then we register the vfs with sqlite
  ret = sqlite3_vfs_register(pVfs, 1);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR: sqlite3_vfs_register, error%d (%s)\n",
          sqlite3_extended_errcode(_sqlite_db),
          sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "MGR-SQLite test:Step #1-register vfs - success\n");
  syslog(1, "MGR-SQLite test:Step #2-find just registered self\n");
  usleep(10 * 1000);

  // Next we attempt to find it...
  // This is really not needed because we already have what we
  // need, but for learning it seems a reasonable thing to do
  sqlite3_vfs *foundVfs = sqlite3_vfs_find(VIRTUAL_FILE_SYS_NAME);
  if(foundVfs == NULL)
  {
    syslog(1, "MGR-SQLite Cannot find vfs:%s, error%d (%s)\n",
          VIRTUAL_FILE_SYS_NAME, sqlite3_extended_errcode(_sqlite_db),
          sqlite3_errmsg(_sqlite_db));
    return -1;
  }
  syslog(1, "MGR-SQLite test:Step #2-find self registered - success\n");
  syslog(1, "MGR-SQLite test:Step #3-open database\n");
  usleep(10 * 1000);


#define DATABASE_NAME "/meadow0/Test1.db"
#define FILE_OF_NAMES "/meadow0/TabDelimited10Names.txt"
#define CREATE_TEST_SQL_TABLE "CREATE TABLE IF NOT EXISTS names (nameid integer,\
 first_name text not null, last_name text not null)"

  // https://sqlite.org/c3ref/open.html
  ret = sqlite3_open_v2(DATABASE_NAME,
          &_sqlite_db,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
          VIRTUAL_FILE_SYS_NAME);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:Cannot open database:%s, error:%d (%s)\n",
          DATABASE_NAME, sqlite3_extended_errcode(_sqlite_db),
          sqlite3_errmsg(_sqlite_db));
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #3-open database-success\n");
//   syslog(1, "MGR-SQLite test:Step #4-turn off journal\n");

// // Turn off journal, at least for now
// #define TEST_SQL_TABLE_NO_JOURNAL "PRAGMA journal_mode=OFF"
//   ret = sqlite3_exec(_sqlite_db, TEST_SQL_TABLE_NO_JOURNAL, NULL, NULL, &errMsg);
//   if( ret != SQLITE_OK )
//   {
//     syslog(1, "MGR-SQLite ERROR:Step #4-Turn off Journal\n");
//     return -1;
//   }
//   syslog(1, "MGR-SQLite test:Step #4-turn off journal - success\n");

  // ------------------------------------
  // This step doesn't work
  // ------------------------------------
  syslog(1, "MGR-SQLite test:Step #5-create database ('%s')\n", CREATE_TEST_SQL_TABLE);
  usleep(10 * 1000);
  
  ret = sqlite3_exec(_sqlite_db, CREATE_TEST_SQL_TABLE, NULL, NULL, &errMsg);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:sqlite3_exec:'%s', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(_sqlite_db),
          sqlite3_errmsg(_sqlite_db));
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #5-create database - success\n");
  syslog(1, "MGR-SQLite test:Step #6-open names file\n");
  usleep(10 * 1000);

  // Get first and last names from a tab-delimited file.
  FILE *pFile = fopen (FILE_OF_NAMES, "r");
  if((void *)pFile == NULL)
  {
    syslog(1, "MGR-SQLite ERROR:fopen:'%s', errno:%d\n",
          FILE_OF_NAMES, errno);
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #6-open names file - success\n");
  usleep(10 * 1000);

  int nameId;
  char sInputBuf[BUFFER_SIZE];
  char *sFirstName;
  char *sLastName;
  char sSQL[256];
  char *pChar;

  syslog(1, "MGR-SQLite test:Step #7 - fill database table with names\n");
  usleep(10 * 1000);
  // Insert names into SQLlite database.
  while (!feof(pFile))
  {
    // Read line
    pChar = fgets (sInputBuf, BUFFER_SIZE, pFile);
    if(pChar == NULL)
    {
      syslog(1, "MGR-SQLite ERROR:fgets %d returned NULL for %s\n", nameId, DATABASE_NAME);
      return -1;
    }

    sFirstName = strtok (sInputBuf, "\t");
    sLastName = strtok (NULL, "\t"); 

    // Insert statment
    sprintf(sSQL, "INSERT INTO Names VALUES (NULL, '%s', '%s')", sFirstName, sLastName);

    syslog(1, "MGR-SQLite test:Step #8 - %d Insert statement '%s'\n", nameId, sSQL);
    
    ret = sqlite3_exec(_sqlite_db, sSQL, NULL, NULL, &errMsg);
    if(ret)
    {
      syslog(1, "MGR-SQLite ERROR:sqlite3_exec INSERT into '%s', errMsg:%s, error:%d (%s)\n",
            DATABASE_NAME, errMsg, sqlite3_extended_errcode(_sqlite_db), sqlite3_errmsg(_sqlite_db));
      return -1;
    }

    nameId++;
  }

  syslog(1, "MGR-SQLite test:Step #9 - closing file and database\n");

  fclose (pFile);
  sqlite3_close(_sqlite_db);
  syslog(1, "MGR-SQLite test:Step #10 - SQLite test exit\n");

  return OK;
}

// These are required for sqlite
SQLITE_API int sqlite3_os_init(void)
{
  syslog(1, "MGR-ENTERED sqlite3_os_init\n");
  return SQLITE_OK;
}

SQLITE_API int sqlite3_os_end(void)
{
  syslog(1, "MGR-ENTERED sqlite3_os_end\n");
  return SQLITE_OK;
}
