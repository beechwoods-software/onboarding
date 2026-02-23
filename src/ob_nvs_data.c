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
    
  if (rc < 0) {
      LOG_ERR("Value not found or error (rc=%d)", rc);
  } else {
      LOG_DBG("Value saved for: %s", name);
  }


  return rc;
}

typedef struct ob_nvs_cb {
  const char * subtree;
} ob_nvs_cb_t;

static int ob_nvs_factory_reset_callback(const char * key,
                                          size_t len,
                                          settings_read_cb read_cb,
                                          void *cb_arg,
                                          void *param)
{

  int rc;
  ob_nvs_cb_t * params = (ob_nvs_cb_t *) param;
  if(NULL != params->subtree) {
    LOG_DBG("Deleting %s/%s",  params->subtree, key);
  } else {
    LOG_DBG("deleting %s", key);
  }

  rc = settings_delete(key);
  if(rc < 0) {
    LOG_ERR("Delete for %s failed %d", key, rc);
  }
  return rc;
}
                                          
void ob_nvs_data_factory_reset()
{
  int rc;
  ob_nvs_cb_t cb = {
    .subtree = NULL
  };

  rc = settings_load_subtree_direct(cb.subtree, ob_nvs_factory_reset_callback, &cb);

  if(rc) {
    LOG_ERR("Factory reset failed %d", rc);
  }
  
}

#endif // CONFIG_ONBOARDING_NVS
