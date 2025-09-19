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
#include <zephyr/fs/nvs.h>

#include "ob_nvs_data.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

/**
 * @brief The nvs file system being used
 */
static struct nvs_fs fs;

/**
 * @brief pointer to the callback function called when data is written.
 */
static ob_nvs_mirror_callback_t mMirrorCallback = NULL;

/**
 * @brief The name in the device tree of the partition which holds
 * The non-volatile data
 */
#define NVS_PARTITION           storage_partition

/**
 * @brief The device where the storage partition resides.
 */
#define NVS_PARTITION_DEVICE    FIXED_PARTITION_DEVICE(NVS_PARTITION)

/**
 * @brief The offset in device where the partition starts
 */
#define NVS_PARTITION_OFFSET    FIXED_PARTITION_OFFSET(NVS_PARTITION)

/**
 * @brief the number of sectors used by the partition
 */
#define NVS_SECTOR_COUNT        3U

/**
 * @brief boolen indicating whether the nvs file system has been initialized.
 */
static bool nvs_data_inited = false;

/**
 * @brief The head of the linked list of known identifiers.
 * @details New items are added at the head
 */
static nvs_record_data_t nvs_head = { 0,0, 0xff} ;

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

  int rc;
  struct flash_pages_info info;

  if (nvs_data_inited) {
    return 0;
  }
  /**
   * @details define the nvs file system by settings with:
   *      sector_size equal to the pagesize,
   *      NVS_SECTOR_COUNT  sectors
   *      starting at NVS_PARTITION_OFFSET
   */

  fs.flash_device = NVS_PARTITION_DEVICE;
  if (!device_is_ready(fs.flash_device)) {
    LOG_ERR("Flash device %s is not ready\n", fs.flash_device->name);
    return -1;
  }
  fs.offset = NVS_PARTITION_OFFSET;
  rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
  if (rc) {
    LOG_ERR("Unable to get page info %d", rc);
    return -1;
  }
  fs.sector_size = info.size;
  fs.sector_count = 3U;

  rc = nvs_mount(&fs);
  if (rc) {
    LOG_ERR("Flash Init failed %d", rc);
    return -1;
  }
  nvs_data_inited = true;
  return 0;
}

/**
 * @brief Read a data element
 */

int ob_nvs_data_read(uint8_t domain, uint8_t id, void * buffer, int len)
{
  int rc;
  nvs_id_t nvs_id;
  nvs_id.domain_id.domain = domain;
  nvs_id.domain_id.id =  id;
  rc = nvs_read(&fs, nvs_id.id, buffer, len);
  if (rc < 0) {
    LOG_ERR("read for %d failed: %d", nvs_id.id, errno);
  }
  return rc;
}

/**
 *@brief Write a data element
 */
int ob_nvs_data_write(uint8_t domain, uint8_t  id, void * buffer, int len)
{
  int rc;
  nvs_id_t nvs_id;
  nvs_id.domain_id.domain = domain;
  nvs_id.domain_id.id =  id;
  LOG_DBG("writing %d %s domain %d id %d domainid 0x%x", len, (char *)buffer, domain, id, nvs_id.id);
  rc = nvs_write(&fs, nvs_id.id, buffer, len);
  if(rc < 0) {
    LOG_ERR("nvs write failed %d for id %d", errno, id);
  } else {
    if(NULL != mMirrorCallback) {
      (*mMirrorCallback)(domain, id, buffer, len);
    }
  }
  return rc;
}

int ob_nvs_data_register_ids(uint8_t domain, uint8_t num)
{
  nvs_record_data_t *rp = &nvs_head;
  while(rp) {
    if(rp->domain == domain) {
      return -EEXIST;
    }
    if( NULL == rp->next) {
      rp->next = malloc(sizeof(nvs_record_data_t));
      if(NULL == rp->next) {
        return -ENOMEM;
      }
      rp = rp->next;
      rp->next = NULL;
      rp->domain = domain;
      rp->num = num;
      return 0;
    }
    rp = rp->next;
  }
  return -EIO;
}
    
      
  

void ob_nvs_data_factory_reset()
{
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
}

#endif // CONFIG_ONBOARDING_NVS
