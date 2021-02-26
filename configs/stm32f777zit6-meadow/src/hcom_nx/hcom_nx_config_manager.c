/****************************************************************************
 * hcom_nx_config_manager.c
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

//  The methods and data structures in this file provide access to the
//  configuration of the meadow board.

#include "hcom_nx_common.h"
#include "../inicfg/meadow_inicfg.h"
#include <meadow/hcom_upd_shared.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/**
 *  Variable to hold a copy of the file name for logging and reporting.
 */
static char *thisFile = __FILE__;

/**
 *  Local variable to hold a ;ointer to the configuration.
 */
static meadow_configuration_t *meadow_configuration = NULL;

/**
 *  Mutex to be used by any code that wants access to the configuration.
 */
static sem_t config_lock;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_nx_get_config_string
 *
 * Description:
 *  Get a string value from the config file.
 *
 * Input Parameters:
 *  section - section of the config file to examine.
 * 
 *  key - key to search for.
 *
 * Returned Value:
 *  Pointer to a copy of the string value.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
char *hcom_nx_get_config_string(char *section, char *key)
{
    char *result = NULL;
    char buffer[128];
    int bufferLength = 128;
    if (meadow_config_find_value_from_key(NULL, section, key, buffer, bufferLength) == 0)
    {
        result = strdup(buffer);
    }
    return(result);
}

/****************************************************************************
 * Name: hcom_nx_config_lock
 *
 * Description:
 *  Lock the specified configuration object.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_config_lock(void)
{
    sem_wait(&config_lock);
}

/****************************************************************************
 * Name: hcom_nx_config_unlock
 *
 * Description:
 *  Unlock the specified configuration object.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_config_unlock(void)
{
    sem_post(&config_lock);
}

/****************************************************************************
 * Name: hcom_nx_get_configuration
 *
 * Description:
 *  Get a pointer to the current configuration.
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *  Pointer to the current configuration object.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
meadow_configuration_t *hcom_nx_get_configuration(void)
{
    return meadow_configuration;
}

/****************************************************************************
 * Name: hcom_nx_read_configuration_file
 *
 * Description:
 *  Read the current configuration from flash and populate the configuration
 *  object.
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *  Pointer to the newly populated configuration object or NULL if there was
 *  a problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
meadow_configuration_t *hcom_nx_read_configuration_file(void)
{
    hcom_nx_config_lock();
    if (meadow_configuration != NULL)
    {
        syslog(LOG_ERR, "%s@%d Attempt to overwrite the configuration object.\n", thisFile, __LINE__);
        return(NULL);
    }

    meadow_configuration = (meadow_configuration_t *) malloc(sizeof(meadow_configuration_t));
    if (meadow_configuration != NULL)
    {
        int error_code;
        memset(meadow_configuration, 0, sizeof(meadow_configuration_t));
        meadow_configuration->mono_trace = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_TRACE_KEY);
        meadow_configuration->mono_debug = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_DEBUG_KEY, 0, &error_code);
        meadow_configuration->mono_run = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_RUN_KEY, 1, &error_code);
        meadow_configuration->trace_level = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_DIAG_TRACE_LEVEL_KEY, 1, &error_code);
        meadow_configuration->uart1_use = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_DIAG_UART_KEY);
        meadow_configuration->esp_spi_speed = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_RESET_ESP32_AT_STARTUP_KEY, MEADOW_INI_CFG_ESP_SPI_SPEED_DEFAULT, &error_code);
        meadow_configuration->device_name = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_TRACE_KEY);
        meadow_configuration->reset_esp32_at_startup = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_RESET_ESP32_AT_STARTUP_KEY, 1, &error_code);
        meadow_configuration->esp_software_version = NULL;
        if (meadow_configuration->device_name == NULL)
        {
            meadow_configuration->device_name = MEADOW_INI_CFG_DEFAULT_DEV_NAME;
        }
    }
    hcom_nx_config_unlock();

    return(meadow_configuration);
}

/****************************************************************************
 * Name: hcom_nx_copy_string
 *
 * Description:
 *  Copy a string into the buffer and return the amount of storage used to
 *  store the string and its terminating 0.
 *
 * Input Parameters:
 *  source - String to be copied.
 *  destination - Memory to hold the copy of the string.
 *
 * Returned Value:
 *  Amount of memory consumed by the string and its terminating 0.
 *
 * Assumptions/Limitations:
 *  The destination buffer is large enough to hold tha copy of the string.
 *
 ****************************************************************************/
int hcom_nx_copy_string(char *source, char *destination)
{
    int length = 0;

    if (source == NULL)
    {
        *destination = 0;
    }
    else
    {
        length = strlen(source);
        strcpy(destination, source);
    }

    return(length + 1);
}

/****************************************************************************
 * Name: hcom_nx_copy_config
 *
 * Description:
 *  Copy the configuration data into the specified location along with copies
 *  of any strings.
 *
 * Input Parameters:
 *  buffer - area of memory to hold the copy of the data in the configuration
 *           structure plus the string.
 *
 * Returned Value:
 *  OK if successful, ERROR otherwise.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int hcom_nx_copy_config_for_user_mode(uint8_t *buffer, int length)
{
    if (buffer == NULL)
    {
        return ERROR;
    }

    int result = OK;
    hcom_nx_config_lock();

    meadow_configuration_t *config = hcom_nx_get_configuration();
    int storage_required = sizeof(meadow_configuration_t);
    if (config->mono_trace != NULL)
    {
        storage_required += strlen(config->mono_trace);
    }
    if (config->device_name != NULL)
    {
        storage_required += strlen(config->device_name);
    }
    if (config->esp_software_version != NULL)
    {
        storage_required += strlen(config->esp_software_version);
    }
    if (config->uart1_use != NULL)
    {
        storage_required += strlen(config->uart1_use);
    }
    if (length < storage_required)
    {
        result = ERROR;
    }
    else
    {
        memset((void *) buffer, 0, length);
        meadow_configuration_t *new_config = (meadow_configuration_t *) buffer;

        memcpy((void *) new_config, (void *) config, sizeof(meadow_configuration_t));
        //
        //  Set the string pointers to NULL in the returned configuration object to make
        //  sure we do not leak the location of these objects in kernel memory.
        //
        new_config->mono_trace = NULL;
        new_config->esp_software_version = NULL;
        new_config->device_name = NULL;
        new_config->uart1_use = NULL;
        //
        //  Put the strings at the end of the configuration structure.
        //
        char *ptr = (char *) (buffer + sizeof(meadow_configuration_t));
        ptr += hcom_nx_copy_string(config->mono_trace, ptr);
        ptr += hcom_nx_copy_string(config->esp_software_version, ptr);
        ptr += hcom_nx_copy_string(config->device_name, ptr);
        ptr += hcom_nx_copy_string(config->uart1_use, ptr);
    }
    
    hcom_nx_config_unlock();

    return(result);
}

/****************************************************************************
 * Name: hcom_nx_config_init
 *
 * Description:
 *  Setup the configuration system and populate the configuration structure
 *  with data from the configuration file.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_config_init(void)
{
    sem_init(&config_lock, 0, 1);
    sem_setprotocol(&config_lock, SEM_PRIO_NONE);
    hcom_nx_read_configuration_file();
}
