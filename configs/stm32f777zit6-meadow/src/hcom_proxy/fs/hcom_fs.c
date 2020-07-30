/****************************************************************************
 * configs/stm32f777-zit6-meadow/src/hcom/hcom_fs.c
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

#include "../hcom_proxy_common.h"

#if !defined CONFIG_FS_LITTLEFS
#warning "LittleFS is not configured. No file system exists."
#endif

#include <errno.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <nuttx/mtd/mtd.h>
#include <dirent.h>


/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
// static struct partition_info
// {
//   struct mtd_dev_s *mtdPart;
//   bool isPartMounted;
//   off_t partPageOffset;
// };

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

// p-m using this would be cleaner
// static FAR struct partition_info _partInfo[HCOM_FLASH_FILE_PARTITION_COUNT_MAX];

static FAR struct mtd_dev_s *_mtd;
static FAR struct mtd_dev_s *_mtdPartArray[HCOM_FLASH_FILE_PARTITION_COUNT_MAX];
static bool _mountedPartitionId[HCOM_FLASH_FILE_PARTITION_COUNT_MAX];
static off_t _partPageOffset[HCOM_FLASH_FILE_PARTITION_COUNT_MAX];
//static bool _shutting_down;
static int _totalPartitionCount;
static uint32_t _pagesPerEraSector;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

int hcom_fs_create_partition_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd,
                                                      uint32_t numbOfPartitions);
int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd, uint32_t numberOfPartitions);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Implementation
 ****************************************************************************/
int hcom_fs_setup(FAR struct mtd_dev_s *mtd)
{
  _mtd = mtd;
  for(int i = 0; i < HCOM_FLASH_FILE_PARTITION_COUNT_MAX; i++)
  {
    _mtdPartArray[i] = NULL;
    _mountedPartitionId[i] = false;
    _partPageOffset[i] = 0;
  }
  
  _totalPartitionCount = 0;
  _pagesPerEraSector = 0;

  // _shutting_down = false;

#if defined(CONFIG_FS_LITTLEFS)
  hcom_fs_littlefs_setup();
#endif

  return OK;
}

// p-m possible?
// //==================================================================
// void hcom_fs_shutdown()
// {
//   _shutting_down = true;

// #if defined(CONFIG_FS_LITTLEFS)
//   hcom_fs_littlefs_shutdown();
// #endif
// }

//==================================================================
// Entry point for initialization of file system
int hcom_fs_init_file_system()
{
  int ret;

#ifdef CONFIG_FS_LITTLEFS
  ret = hcom_little_support_init_master_fs(_mtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-ERROR:LittleFS F/S init error:%d\n",
              thisFile, __LINE__, ret);
    return ret;
  }
#endif

#if !defined(CONFIG_HCOM_MTD_STRESS_TEST)
  ret = hcom_fs_create_partition_initialize_and_mount_fs(_mtd,
        HCOM_NUMBER_OF_FS_PARTITIONS);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-F/S creation, error:%d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

  return OK;
}

//=====================================================================
// This function will create partitions, initialize the file system, mount
// the partititions and if necessary format the partitions.
int hcom_fs_create_partition_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd,
                                                      uint32_t numbOfPartitions)
{
  int ret;
  uint32_t partCounter;

  //------------------------------------------------------------------------
  syslog(LOG_INFO, "F/S create %d partitions\n", numbOfPartitions);

  // Create the number of partitions in the external flash
  ret = hcom_fs_init_partitions(mtd, numbOfPartitions);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-partitioning FS:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  //------------------------------------------------------------------------
  syslog(LOG_INFO, "Partitioning complete. Init partitions\n");

  // Initialize the file system
  for (partCounter = 0; partCounter < numbOfPartitions; partCounter++)
  {
    syslog(2, "Init F/S part:%d\n", partCounter);

#if (defined CONFIG_FS_LITTLEFS && defined CONFIG_MTD_PARTITION)
    ret = hcom_fs_littlefs_init_part_fs(partCounter, _mtdPartArray[partCounter]);
    if (ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-LittleFS error:%d part:%d\n",
               thisFile, __LINE__, ret, partCounter);
      return ret;
    }
#endif
  }

  //------------------------------------------------------------------------
  syslog(LOG_INFO, "F/S Part Init complete, mount & format\n");

  for (partCounter = 0; partCounter < numbOfPartitions; partCounter++)
  {
    syslog(2, "%s@%d-Mount part %d\n", thisFile, __LINE__, partCounter);

    // Attempt to mount - if fails format and attempt to mount again
#if defined(CONFIG_FS_LITTLEFS)
    ret = hcom_fs_littlefs_mount_format(partCounter);
    if (ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-mount failed '%s' to '%s' for type '%s', Part %d error:%d\n",
            thisFile, __LINE__, HCOM_FILE_MOUNT_POINT_SOURCE, HCOM_FILE_MOUNT_POINT_TARGET,
            HCOM_FILE_MOUNT_FILE_SYS_TYPE, partCounter, ret);
    }
#endif
  }

  syslog(LOG_INFO, "F/S creation complete\n");
  return OK;
}

//==============================================================================
// This method is called to mount either one partition or the only file system 
int hcom_fs_mount_file_system(const char *sourceDevice, const char *targetDevice,
                                        const char *fileSystemType, uint32_t partitionId,
                                        const char *mountCommand)
{
  // if (_shutting_down)
  //   return OK;

  int ret;
  char *finalSourceName = malloc(HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  char *fullMountPtName = malloc(HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);

#ifdef CONFIG_MTD_PARTITION
  // e.g. /dev/smart0 or dev/little0
  int stringLen = snprintf(finalSourceName, HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s0p%d", sourceDevice, partitionId);
  DEBUGASSERT(stringLen < HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  // e.g. /meadow0
  stringLen = snprintf(fullMountPtName, HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s%d", targetDevice, partitionId);
  DEBUGASSERT(stringLen < HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);

  syslog(LOG_INFO, "Attempt to mount partition %d as '%s' to '%s' type '%s'\n",
           partitionId, finalSourceName, fullMountPtName, fileSystemType);
#else
  // e.g. mount("/dev/little", "/meadow", "littlefs", 0, NULL);
  DEBUGASSERT(strlen(sourceDevice) < HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  strcpy(finalSourceName, sourceDevice);
  DEBUGASSERT(strlen(targetDevice) < HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  strcpy(fullMountPtName, targetDevice);

  syslog(LOG_INFO, "Mounting '%s' to '%s' type '%s'\n",
           finalSourceName, fullMountPtName, fileSystemType);
#endif

  // e.g. mount("/dev/ram0", "/mnt", "vfat", 0, NULL);  // Needs backing block device
  // e.g. mount(NULL, "/mnt", "nxffs", 0, NULL);        // When no backing block device
  ret = mount(finalSourceName, fullMountPtName, fileSystemType, 0, mountCommand);
  if (ret < 0)
  {
    // The mount function puts the error code into errno
    int errnumb = get_errno();
    // Return to the specific file system code to determine if formatting needed
    free(finalSourceName);
    free(fullMountPtName);
    return -errnumb;
  }

  _mountedPartitionId[partitionId] = true;
  
  syslog(LOG_INFO, "%s@%d-Mounted '%s' to '%s' for type '%s'\n", thisFile, __LINE__,
        finalSourceName, fullMountPtName, fileSystemType);
        
  free(finalSourceName);
  free(fullMountPtName);

  return OK;
}

//=====================================================================
//
bool hcom_fs_is_mounted(uint32_t partitionId)
{
  return (_mountedPartitionId[partitionId]);
}

//=====================================================================
//
int hcom_fs_1st_erase_sector_of_partition(uint32_t partitionId)
{
  return (_partPageOffset[partitionId] / _pagesPerEraSector);
}

//======================================================================
// Partitions the entire flash chip with the number of partitions specified.
// This sets the size of each partition
int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd, uint32_t numberOfPartitions)
{
#ifndef CONFIG_MTD_PARTITION
  _mtdPartArray[0] = mtd;
  _totalPartitionCount = 1;
#else
  FAR struct mtd_geometry_s geo;
  int partitionId;

  _totalPartitionCount = numberOfPartitions;
  if (numberOfPartitions > HCOM_FLASH_FILE_PARTITION_COUNT_MAX)
  {
    syslog(LOG_ERR, "%s@%d-%d parts too big %d max\n",
             thisFile, __LINE__, numberOfPartitions, HCOM_FLASH_FILE_PARTITION_COUNT_MAX);
    return -1;
  }

  // Get geometry of QSPI Flash
  int ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Read MTD geometry:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  syslog(2, "MTD Geo info - numb erase sectors %u, erasesize %u page size %u\n",
          geo.neraseblocks, geo.erasesize, geo.blocksize);

  _pagesPerEraSector = geo.erasesize / geo.blocksize;

  size_t nEraseBlocks = geo.neraseblocks;
  nEraseBlocks -= (HCOM_FS_MONO_RAW_PARTITION_SIZE / geo.erasesize);

  int nPages = (nEraseBlocks / numberOfPartitions) * _pagesPerEraSector;

  // Reserve some size in the flash for Mono raw partition.
  int offsetInPages = HCOM_FS_MONO_RAW_PARTITION_SIZE / geo.blocksize;

  size_t partsize = nPages * geo.blocksize;

  for (partitionId = 0; partitionId < numberOfPartitions; partitionId++)
  {
    _partPageOffset[partitionId] = offsetInPages;
    _mtdPartArray[partitionId] = mtd_partition(mtd, offsetInPages, nPages);
    offsetInPages += nPages;
    if (!_mtdPartArray[partitionId])
    {
      syslog(LOG_ERR, "%s@%d-Error:mtd_partition, offset=%lu nPages=%lu\n",
               thisFile, __LINE__, (unsigned long)offsetInPages, (unsigned long)nPages);
    }

    syslog(LOG_INFO, "Part %d created offset:%lu size:%d bytes\n",
             partitionId, (unsigned long)offsetInPages, partsize);
  }
#endif
}
