/*
 * Copyright 2024 Beechwoods Software, Inc brad@beechwoods.com
 * All Rights Reserved
 */

#ifdef CONFIG_ONBOARDING_OTA

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "ob_ota.h"
#include "ob_nvs_data.h"

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME);


int ota_poll_interval = 0;
/*
 * the network must be up before the call to ota_init can be made 
#define OTA_EVENT_MASK (NET_EVENT_L4_CONNECTED | \
		    NET_EVENT_L4_DISCONNECTED)
*/
static ob_ota_vtable_t *ota_vtable = NULL;

void
ota_set_vtable(ob_ota_vtable_t * table)
{
  ota_vtable = table;
}
void
ota_set_poll_interval(int interval)
{
  ota_poll_interval = interval;
}

void ota_reboot(void) {

  if((ota_vtable) && ( ota_vtable->ob_ota_reboot)) {
    LOG_DBG("Calling ota reboot");
    ota_vtable->ob_ota_reboot();
  } else {
    sys_reboot(SYS_REBOOT_COLD);
  }
}  

bool ota_do_update()
{
  bool rc = false;
  ob_ota_result_t status;

  if(ota_vtable->ob_ota_update()) {
    status = ota_vtable->ob_ota_update();
    switch(status) {
    case OB_OTA_RES_OK:
      LOG_PANIC();
      ota_reboot();
      rc = true;
      break;
    case OB_OTA_RES_NETWORK_ERROR:
    case OB_OTA_RES_DOWNLOAD_ERROR:
    case OB_OTA_RES_INSTALL_ERROR:
    case OB_OTA_RES_FLASH_INIT_ERROR:
    default:
      LOG_ERR("Unknown update response %d",status);
      break;
    }
  }
  return rc;
}
      
  

bool ota_init()
{
  bool rc = true;
  int ret;
  ob_ota_result_t status;
#ifdef CONFIG_ONBOARDING_OTA_UPDATEHUB
  rc = ota_init_updatehub();
#endif // CONFIG_ONBOARDING_OTA_UPDATEHUB
#ifdef CONFIG_ONBOARDING_OTA_HAWKBIT
  rc = ota_init_hawkbit();
#endif //CONFIG_ONBOARDING_OTA_HAWKBIT
#ifdef CONFIG_ONBOARDING_OTA_MENDER
  rc = ota_init_mender();
#endif // CONFIG_ONBOARDING_OTA_MENDER
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
  rc = ota_init_golioth();
#endif
  if(false == rc) {
    LOG_ERR("OTA provider failed initialization");
    return rc;
  }



  
  if(NULL == ota_vtable) {
    LOG_ERR("OTA provider not initialized");
    return false;
  }
  if(NULL != ota_vtable->ob_ota_confirm) {
    ret = ota_vtable->ob_ota_confirm();
    if(ret < 0) {
      LOG_ERR("Update confirm failed %d", ret);
    }
  }
  if(ota_poll_interval > 0) {
    //   polling mode
    LOG_DBG("OTA pooling every %d minutes", ota_poll_interval);
    if(ota_vtable->ob_ota_autohandler) {
      ota_vtable->ob_ota_autohandler();
    }
  } else {
    if(ota_vtable->ob_ota_probe) {
      status = ota_vtable->ob_ota_probe();
      switch(status) {
      case OB_OTA_RES_HAS_UPDATE:
        rc = ota_do_update();
        break;
      case OB_OTA_RES_NO_UPDATE:
        LOG_DBG("No update");
        break;
      case OB_OTA_RES_NETWORK_ERROR:
        LOG_ERR("Networking error");
        rc = false;
      case OB_OTA_RES_INCOMPATIBLE_HARDWARE:
        LOG_ERR("Incomaptible hardware");
        rc = false;
        break;
      case OB_OTA_RES_METADATA_ERROR:
        LOG_ERR("Failed to parse meta data");
        rc = false;
        break;
      case OB_OTA_RES_UNCONFIRMED_IMAGE:
        LOG_ERR("Thie image is not confirmed");
        rc = false;
        break;
      default:
        LOG_WRN("Unknown status %d", status);
        rc = false;
        break;
      }
    }
  }
  return rc;
}

#ifdef CONFIG_ONBOARDING_DT

int ob_dt_set(const char *key, const char * value)
{
  int rc = -1;
  LOG_DBG("DT SET k: %s v:%s", key, value);
  if(ota_vtable->ob_dt_set) {
    rc = ota_vtable->ob_dt_set(key, value);
  }
  return rc;
}

#endif // CONFIG_ONBOARDING_DT

#endif // CONFIG_ONBOARDING_OTA
