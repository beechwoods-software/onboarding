/*
 * Copyright Beechwoods Software, Inc. 2023 Brad Kemp
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
 */

#ifdef CONFIG_ONBOARDING_NVS
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>

#include "ob_nvs_data.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

/**
 * @brief pointer to the callback function called when data is written.
 */
static ob_nvs_mirror_callback_t mMirrorCallback = NULL;

/**
 * @brief boolen indicating whether the nvs file system has been initialized.
 */
static bool nvs_data_inited = false;

/**
 *  @details This fucntion sets the address of a callback function which is called when data is written to nvs.
 */
void ob_nvs_set_mirror_callback(ob_nvs_mirror_callback_t callback) {
  mMirrorCallback = callback;
}

/**
 * @details create a nvs_fs structure and mount the defined file system.
 */
int
ob_nvs_data_init(void)
{

  int rc=0;

  if (nvs_data_inited) {
    return 0;
  }

  settings_subsys_init();
  nvs_data_inited = true;

  return rc;
}

/**
 * @brief Read a data element
 */

int ob_nvs_data_read(const char * name, void * buffer, int len)
{
  int rc=0;
  LOG_DBG("reading from name=%s", name);

  rc = settings_load_one(name, buffer, len);

  if (rc > 0) {
      LOG_DBG("Value loaded for: %s", name);
  } else {
      LOG_ERR("Value not found or error (rc=%d)", rc);
  }

  return rc;
}

/**
 *@brief Write a data element
 */
int ob_nvs_data_write(const char * name, void * buffer, int len)
{
  int rc=0;
  LOG_DBG("writing len=%d buffer=%s to name=%s", len, (char *)buffer, name);

  rc = settings_save_one(name, buffer, len);
    
  if (rc > 0) {
      LOG_DBG("Value saved for: %s", name);
  } else {
      LOG_ERR("Value not found or error (rc=%d)", rc);
  }


  return rc;
}

void ob_nvs_data_factory_reset()
{
#if 0    
  uint16_t i;
  int rc;
  uint8_t dummy;
  nvs_id_t id;
  nvs_record_data_t *rp = &nvs_head;
  while(rp) {
    id.domain_id.domain = rp->domain;
    for(i = 0; i < rp->num; i++) {
      id.domain_id.id = i;
      if(0 == id.domain_id.domain) {
        // Try to delete any external nvs usage (assumes ids are contiguous)
        if(ob_nvs_data_read(0, i, &dummy, sizeof(dummy)) < 0) {
          break;
        }
      } 
      rc = nvs_delete(&fs, id.id);
      if(rc < 0) {
        LOG_ERR("Delete of id %d failed :%d", i,  errno);
      }
    }
    rp = rp->next;
  }
#endif  
}

#endif // CONFIG_ONBOARDING_NVS
