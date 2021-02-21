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
 * Name: hcom_nx_get_default_configuration
 *
 * Description:
 *  Get the default configuration object for the board.
 *
 * Input Parameters:
 *  None
 *
 * Returned Value:
 *  Pointer to a valid meadow configuration object, NULL if there was a problem
 *  allocating memory.
 *
 * Assumptions/Limitations:
 *  This is only called once and so we do not need to lock the configuration
 *  object.
 *
 ****************************************************************************/
meadow_configuration_t *hcom_nx_get_default_configuration(void)
{
    meadow_configuration_t *config = (meadow_configuration_t *) malloc(sizeof(meadow_configuration_t));
    if (config != NULL)
    {
        memset(config, 0, sizeof(meadow_configuration_t));
        sem_init(&config->lock, 0, 1);
        sem_setprotocol(&config->lock, SEM_PRIO_NONE);
        config->mono_trace = NULL;
        config->mono_debug = false;
        config->trace_level = 0;
        config->uart1_use = NULL;
        config->esp_spi_speed = 12000000;
        config->device_name = "MeadowF7";
    }
    return (config);
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
meadow_configuration_t *hcom_nx_read_configuration_file()
{
    if (meadow_configuration != NULL)
    {
        syslog(LOG_ERR, "%s@%d Attempt to overwrite the configuration object.\n", thisFile, __LINE__);
        return(NULL);
    }
    meadow_configuration = hcom_nx_get_default_configuration();
}