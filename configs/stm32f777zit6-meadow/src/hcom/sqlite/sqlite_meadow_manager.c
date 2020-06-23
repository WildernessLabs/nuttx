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
 To starting July 3 on return from vacation:
 1. Clean up sqlite_meadow_vfs.c
 2. Clean up sqlite_meadow_manager.c
 3. Investigate .Net options for interacting with SQLite
 4. Get busy planning and doing the Meadow.Core integration
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "sqlite3.h"

#include "../hcom_common.h"

#define DATABASE_NAME "/meadow0/Test1.db"

sqlite3_vfs *sqlite3_demovfs(void);

static int execute_sqlite_database_experiment(void);
static int execute_sqlite_query1_experiment(void);
static int execute_sqlite_query2_experiment(void);

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
  switch(userData)
  {
    case 1:
    execute_sqlite_query1_experiment();
    break;
    
    case 2:
    execute_sqlite_query2_experiment();
    break;

    default:
    break;
  }
}

void hcom_exec_rqst_testing_developer_3(uint32_t userData)
{
}

void hcom_exec_rqst_testing_developer_4(uint32_t userData)
{
}

//================================================================
// There are several hardcode and #defined objects used here
static sqlite3 * hcom_sqlite_register_vfs_and_open(const char *databaseName)
{
#define VIRTUAL_FILE_SYS_NAME "demo"

  sqlite3 *sqlite_db;
  int ret;
    
  syslog(1, "MGR-SQLite open database ENTERED\n");

  // Must register the vfs since we cannot use one of the built-in ones
  // To do this we ask the vfs for a pointer that represents itself.
  syslog(1, "MGR-SQLite test:Step #1-register vfs\n");

  // For now we'll try to use the demo vfs (at least it builds)
  sqlite3_vfs *pVfs = sqlite3_demovfs();
  if(pVfs == NULL)
  {
    syslog(1, "MGR-SQLite ERROR: sqlite3_demovfs\n");
    return NULL;
  }
  syslog(1, "MGR-SQLite test:Step #1-register vfs - success\n");
  syslog(1, "MGR-SQLite test:Step #2-find just registered self\n");

  // Then we register the vfs with sqlite
  ret = sqlite3_vfs_register(pVfs, 1);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR: sqlite3_vfs_register\n");
    return NULL;
  }
  syslog(1, "MGR-SQLite test:Step #1-register vfs - success\n");
  syslog(1, "MGR-SQLite test:Step #2-find just registered self\n");

  // Next we attempt to find it...
  // This is really not needed because we already have what we
  // need, but for learning it seems a reasonable thing to do
  sqlite3_vfs *foundVfs = sqlite3_vfs_find(VIRTUAL_FILE_SYS_NAME);
  if(foundVfs == NULL)
  {
    syslog(1, "MGR-SQLite Cannot find vfs:%s)\n",
          VIRTUAL_FILE_SYS_NAME);
    return NULL;
  }
  syslog(1, "MGR-SQLite test:Step #2-find self registered - success\n");
  syslog(1, "MGR-SQLite test:Step #3-open database\n");

  //-------------------------------------------------------
  // Open database
  // https://sqlite.org/c3ref/open.html
  ret = sqlite3_open_v2(databaseName,
          &sqlite_db,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
          VIRTUAL_FILE_SYS_NAME);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:Cannot open database:%s, error:%d (%s)\n",
          databaseName, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return NULL;
  }

  syslog(1, "MGR-SQLite test:Step #3-open database-success\n");
  
  return sqlite_db;
}

//=========================================================
// Called from cli for testing
int execute_sqlite_database_experiment()
{
  sqlite3 *sqlite_db;
  int ret;
  char *errMsg;
    
  syslog(1, "MGR-SQLite create and insert test:Started\n");
  sqlite_db = hcom_sqlite_register_vfs_and_open(DATABASE_NAME);
  if(sqlite_db == NULL)
  {
    syslog(1, "MGR-SQLite ERROR:Call to hcom_sqlite_register_vfs_and_open())\n",
          DATABASE_NAME);
  }

// // Turn off journal, at least for now
// #define TEST_SQL_TABLE_NO_JOURNAL "PRAGMA journal_mode=OFF"
//   ret = sqlite3_exec(sqlite_db, TEST_SQL_TABLE_NO_JOURNAL, NULL, NULL, &errMsg);
//   if( ret != SQLITE_OK )
//   {
//     syslog(1, "MGR-SQLite ERROR:Step #4-Turn off Journal\n");
//     return -1;
//   }
//   syslog(1, "MGR-SQLite test:Step #4-turn off journal - success\n");

  char *tableDropSql = 
    "DROP TABLE IF EXISTS Names";

  syslog(1, "MGR-SQLite test:Step #4-Drop table if it exists [%s]\n", tableDropSql);
  ret = sqlite3_exec(sqlite_db, tableDropSql, NULL, NULL, &errMsg);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:Drop table-sqlite3_exec:'%s', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }

   char *tableCreateSql = 
    "CREATE TABLE Names "
    "(nameid integer,"
    " first_name text not null,"
    " last_name text not null)";
 
  syslog(1, "MGR-SQLite test:Step #5-create database [%s]\n", tableCreateSql);

  ret = sqlite3_exec(sqlite_db, tableCreateSql, NULL, NULL, &errMsg);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:Create table-sqlite3_exec:'%s', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #5-create database - success\n");
  //--------------------------------------------------
  syslog(1, "MGR-SQLite test:Step #6-open names file\n");

  #define FILE_OF_NAMES "/meadow0/TabDelimited200Names.txt"
  // Get first and last names from a tab-delimited file.
  FILE *pFile = fopen (FILE_OF_NAMES, "r");
  if((void *)pFile == NULL)
  {
    syslog(1, "MGR-SQLite ERROR:fopen:'%s', errno:%d\n",
          FILE_OF_NAMES, errno);
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #6-open names file - success\n");

#define BUFFER_SIZE 256
  int nameId = 0;
  char sFirstName[64];
  char sTab[4];
  char sLastName[64];
  char sSQL[256];

  ret = sqlite3_exec(sqlite_db, "BEGIN TRANSACTION", NULL, NULL, &errMsg);
  if(ret < 0)
  {
    syslog(1, "MGR-SQLite ERROR:sqlite3_exec:'BEGIN TRANSACTION', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }

  syslog(1, "MGR-SQLite test:Step #7 - fill database table with names\n");
  //--------------------------------------------------
  // Insert names into SQLlite database.
  while(true)
  {
    nameId++;

    // Read a line
    ret = fscanf(pFile, "%s%c%s", sFirstName, sTab, sLastName);
    if(ret != 3)
    {
      break;
    }

    // Insert statement
    sprintf(sSQL, "INSERT INTO Names VALUES (%d, '%s','%s')", nameId, sFirstName, sLastName);
    // syslog(1, "--------------------------------------\n");
    syslog(1, "MGR-SQLite test:Step #8 (%d)-Insert statement '%s'\n", nameId, sSQL);

    // Could speed up by using prepare etc.
    ret = sqlite3_exec(sqlite_db, sSQL, NULL, NULL, &errMsg);
    if(ret)
    {
      syslog(1, "MGR-SQLite ERROR:sqlite3_exec INSERT into '%s', errMsg:%s, error:%d (%s)\n",
            DATABASE_NAME, errMsg, sqlite3_extended_errcode(sqlite_db), sqlite3_errmsg(sqlite_db));
      return -1;
    }
  }

  ret = sqlite3_exec(sqlite_db, "END TRANSACTION", NULL, NULL, &errMsg);
  if(ret < 0)
  {
    syslog(1, "MGR-SQLite ERROR:sqlite3_exec:'END TRANSACTION', errMsg:%s, error:%d (%s)\n",
          DATABASE_NAME, errMsg, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }
  
  if(ret < 0)
  {
    if(errno != 0)
    {
      syslog(1, "MGR-SQLite test:Step #8 ERROR:ret:%d, errno:%d\n", ret, errno);
      return -errno;
    }
  }

  sqlite3_close_v2(sqlite_db);

  syslog(1, "MGR-SQLite test:Step #8 SUCCESS wrote %d entries\n", nameId);
  return OK;
}

//===========================================================
int execute_sqlite_query1_experiment()
{
  sqlite3 *sqlite_db;
  int ret;
    
  syslog(1, "MGR-SQLite create and insert test:Started\n");
  sqlite_db = hcom_sqlite_register_vfs_and_open(DATABASE_NAME);
  if(sqlite_db == NULL)
  {
    syslog(1, "MGR-SQLite ERROR:Call to hcom_sqlite_register_vfs_and_open())\n",
          DATABASE_NAME);
  }

  syslog(1, "MGR-SQLite test:Step #3-read database\n");
  
  //--------------------------------------------------------
  sqlite3_stmt *stmt;
  const char *simpleQuerySql = 
    "SELECT * "
    "FROM Names "
    "ORDER BY last_name";

  ret = sqlite3_prepare_v2(sqlite_db, simpleQuerySql, -1, &stmt, NULL);
  if(ret)
  {
    syslog(1, "MGR-SQLite ERROR:sqlite3_prepare_v3:'%s', error:%d (%s)\n",
          DATABASE_NAME, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }

  int counter = 1;
  while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
  {
    // show the data
    int id = sqlite3_column_int (stmt, 0);
    const unsigned char *firstName = sqlite3_column_text (stmt, 1);
    const unsigned char *lastName = sqlite3_column_text (stmt, 2);

    syslog(1, "#%d-Row:%d, First:%s Name:%s\n", counter, id, firstName, lastName);
    counter++;
  }

  if(ret != SQLITE_DONE)
  {
    syslog(1, "MGR-SQLite ERROR:sqlite3_step:'%s', error:%d (%s)\n",
          DATABASE_NAME, sqlite3_extended_errcode(sqlite_db),
          sqlite3_errmsg(sqlite_db));
    return -1;
  }
  sqlite3_finalize(stmt);
  sqlite3_close_v2(sqlite_db);

  syslog(1, "MGR-SQLite test:Step #3-read database - success\n");
  return OK;
}

//===========================================================
int execute_sqlite_query2_experiment()
{
  return OK;
}

//===========================================================
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
