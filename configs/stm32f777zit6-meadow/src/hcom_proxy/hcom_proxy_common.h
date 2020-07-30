/****************************************************************************
 * configs/stm32f777-zit6-meadow/src/hcom/hcom_common.h
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
 *   Copyright (C) 2017 Gregory Nutt. All rights reserved.
 *   Copyright (C) 2017 Alan Carvalho de Assis. All rights reserved.
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
#ifndef __CONFIGS_MEADOW_SRC_HCOM_PROXY_COMMON__H
#define __CONFIGS_MEADOW_SRC_HCOM_PROXY_COMMON__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __ASSEMBLY__
#include <stdint.h>
#endif
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <crc32.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/board.h>
#include <nuttx/mm/mm.h>
#include <nuttx/config.h>
#include <limits.h>

#include <nuttx/arch.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/userspace.h>
#include <nuttx/kthread.h>

#include "hcom_proxy_shared.h"

#ifndef OK
  #define OK 0
#endif

#ifndef __ASSEMBLY__

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#define HCOM_FLASH_FILE_PARTITION_COUNT_MAX 8

// PATH_MAX is defined by Nuttx in limits.h. It's 256 or less
#define HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH ((PATH_MAX * 2) + 2) // allocate

#ifdef CONFIG_MTD_PARTITION
#define HCOM_NUMBER_OF_FS_PARTITIONS 1    // Any number 2 - 8
#else
#define HCOM_NUMBER_OF_FS_PARTITIONS 1    // 1 if no partitions in use
#endif

#define HCOM_FS_MONO_RAW_PARTITION_SIZE 0x200000 // 2MB
#define HCOM_FS_MONO_RUNTIME_FILENAME "Meadow.OS.Runtime.bin"

// "/meadow" is shared by all supported file systems
#define HCOM_FILE_MOUNT_POINT_TARGET "/meadow"

#ifdef CONFIG_FS_LITTLEFS
#define HCOM_FILE_MOUNT_FILE_SYS_TYPE "littlefs"
#define HCOM_FILE_MOUNT_POINT_SOURCE "/dev/little"
#define HCOM_FILE_MOUNT_FORCE_FORMAT "forceformat"
#endif

/****************************************************************************************************
 * Public Functions
 ****************************************************************************************************/

  // int hcom_proxy_setup_mgr(FAR struct mtd_dev_s *mtd);   // called from stm32_boot.c
  int hcom_utils_setup(void);
  int hcom_proxy_upd_initialize(void);

  // HCOM command handling
  int hcom_proxy_route_cli_command(struct hcom_proxy_cmd_data *cmdData);

  // External flash
  int hcom_exec_ex_flash_setup(FAR struct mtd_dev_s *mtd);
  int hcom_exec_ex_flash_mono_flash(struct hcom_proxy_cmd_data *cmd_data);
  int hcom_exec_ex_flash_erase_ex_flash(struct hcom_proxy_cmd_data *cmdData);
  int hcom_exec_ex_flash_verify_ex_flash(struct hcom_proxy_cmd_data *cmdData);

  // Low-level file system
  int hcom_fs_setup(FAR struct mtd_dev_s *mtd);
  int hcom_fs_init_file_system(void);
  int hcom_fs_mount_file_system(const char *sourceDevice, const char *targetDevice,
                                        const char *fileSystemType, uint32_t partitionId,
                                        const char *mountCommand);

#ifdef CONFIG_FS_LITTLEFS
  int hcom_fs_littlefs_setup(void);
  int hcom_little_support_init_master_fs(FAR struct mtd_dev_s *master_flash_mtd);
  void hcom_fs_littlefs_shutdown(void);
#ifdef CONFIG_MTD_PARTITION
  int hcom_fs_littlefs_init_part_fs(uint32_t partitionId, struct mtd_dev_s *partMtd);
#endif
  int hcom_fs_littlefs_mount_format(uint32_t partitionId);
#endif

  // Diagnostics
  void hcom_utils_diag_print_buffer(const uint8_t buffer[], const int bufLen, uint8_t msgPriority);

#define HCOM_COMMON_UTILS_DIAG_PRINT_BUFFER 0

#endif // __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif // __CONFIGS_MEADOW_SRC_HCOM_PROXY_COMMON__H
