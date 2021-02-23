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
static char *thisFile = __FILE__;

static meadow_configuration_t *meadow_configuration = NULL;

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
 *  config - Pointer to an meadow_configuration_t object to be locked.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_config_lock(meadow_configuration_t *config)
{
    sem_wait(&config->lock);
}

/****************************************************************************
 * Name: hcom_nx_config_unlock
 *
 * Description:
 *  Unlock the specified configuration object.
 *
 * Input Parameters:
 *  config - Pointer to an meadow_configuration_t object to be locked.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_config_unlock(meadow_configuration_t *config)
{
    sem_post(&config->lock);
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
        sem_init(&meadow_configuration->lock, 0, 1);
        sem_setprotocol(&meadow_configuration->lock, SEM_PRIO_NONE);
        meadow_configuration->mono_trace = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_TRACE_KEY);
        meadow_configuration->mono_debug = (meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_DEBUG_KEY, 0, &error_code) == 1);;
        meadow_configuration->trace_level = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_DIAG_TRACE_LEVEL_KEY, 1, &error_code);
        meadow_configuration->uart1_use = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_DIAG_UART_KEY);
        meadow_configuration->esp_spi_speed = meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_RESET_ESP32_AT_STARTUP_KEY, MEADOW_INI_CFG_ESP_SPI_SPEED_DEFAULT, &error_code);
        meadow_configuration->device_name = hcom_nx_get_config_string(MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_MONO_TRACE_KEY);
        meadow_configuration->reset_esp32_at_startup = (meadow_ini_cfg_get_int_default(NULL, MEADOW_INI_CFG_STARTUP_SECTION, MEADOW_INI_CFG_RESET_ESP32_AT_STARTUP_KEY, 1, &error_code) == 1);
        if (meadow_configuration->device_name == NULL)
        {
            meadow_configuration->device_name = MEADOW_INI_CFG_DEFAULT_DEV_NAME;
        }
    }
    return(meadow_configuration);
}

/****************************************************************************
 * Name: hcom_nx_copy_config
 *
 * Description:
 *  Copy the configuration data into the specified location.
 *  object.
 * 
 *  Note that not all of the configuration data is copied as some of the
 *  data is only relevant to objects that exist in kernel space.
 *
 * Input Parameters:
 *  new_config - pointer to the memory that will hold a copy of the
 *               configuration data.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void hcom_nx_copy_config_for_user_mode(meadow_configuration_t *new_config)
{
    if (new_config == NULL)
    {
        return;
    }
    memset((void *) new_config, 0, sizeof(meadow_configuration_t));

    meadow_configuration_t *config = hcom_nx_get_configuration();
    if (config == NULL)
    {
        return;
    }
    
    hcom_nx_config_lock(config);
    new_config->mono_debug = config->mono_debug;
    new_config->reset_esp32_at_startup = config->reset_esp32_at_startup;
    new_config->device_name = strdup(config->device_name);
    if (config->mono_trace != NULL)
    {
        new_config->mono_trace = strdup(config->mono_trace);
    }
    else
    {
        new_config->mono_trace = NULL;
    }
    if (config->uart1_use != NULL)
    {
        new_config->uart1_use = strdup(config->uart1_use);
    }
    else
    {
        new_config->uart1_use = strdup(config->uart1_use);
    }
    hcom_nx_config_unlock(config);
}