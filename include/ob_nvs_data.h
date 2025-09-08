/*
 * Copyright Beechwoods Software, Inc. 2023 Brad Kemp
 * All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#ifdef CONFIG_ONBOARDING_NVS
#define ONBOARDING_LOG_MODULE_NAME onboarding

/**
 * @brief Represent an nvs identifier @n
 * @struct _nvs_domain
 * The nvs identifier is composed of a domain which defines the module that data
 * is associated with and an data id that reprents which
 * data record in the domain.
 */
typedef struct _nvs_domain {
  /** @brief The domain identifier */
  uint8_t domain;
  /** @brief The data record identifier */
  uint8_t id;
} nvs_domain_id_t;

/**
 * @brief A union of the nvs identifier and a 16 bit data element. @n
 * @union _nvs_id
 * This union is used to easily convert between domain/id tuples and a 16 bit NVS identifier
 */
typedef union _nvs_id {
  /** @brief the domain/id tuple */
  nvs_domain_id_t domain_id;
  /** @brief The 16 bit identifier used by the NVS file system.*/
  uint16_t id;
} nvs_id_t;

/**
 * @brief An element in a linked list of known NVS data record types.
 * @struct _nvs_record_data
 * The struct is used to track the known NVS data record types to allow their erasure on a factory reset.
 * This item is created during the ob_nvs_data_register_ids call.
 * @see ob_nvs_data_register_ids
 */
typedef struct _nvs_record_data
{
  /** @brief a pointer to the next data element */
  struct _nvs_record_data * next;
  /** @brief The domain of this element */
  uint8_t domain;
  /** @brief The number of data record identifiers. Identifiers start at 0 */
  int num;
} nvs_record_data_t;

/**
 * @brief initialize flash file system
 * This function initilizes a flash file system
 * found on the storage_partion dts entry. @n
 * If the file system does not exist it is created
 * @return 0 on success
 * @return -1 on failure
 */
int ob_nvs_data_init(void);

/**
 * @brief Register flash IDs for a domain @n
 * This function registers flash Ids for a domain. @n
 * An ID contains a domain identifier and a data identifier. @n
 * The NVS ID is created by shifting the domain identfier by 8 and oring in the data identifier. @n
 * IDs registered in this manner will be erased during a factory reset
 * @param domain - the domain identifier
 * @param num - the number of flash IDs in this domain
 * @return 0 success
 * @return -EEXIST domain already exists
 * @return -ENOMEM if memory allocation fails
 * @return -EIO on unscpecified error
 */
int ob_nvs_data_register_ids(uint8_t domain, uint8_t num);

/**
 * @brief This function reads a record from  nvs storage
 *
 * @param domain - the domain of the data record
 * @param id - the ID of the data record
 * @param buffer - the buffer to copy the data to
 * @param len - the lenght of the buffer
 *
 * @return 0 on success
 * @return -1 on error - see errno
 **/
int ob_nvs_data_read(uint8_t domain, uint8_t id, void * buffer, int len);

/**
 * @brief write a record to the nvs store
 * This function writes data to the nvs partition
 * If the record already exists it is overwritten.
 * If the record does not exist it is created.
 * @param domain - the domain of the data record
 * @param id - the ID of the data record
 * @param buffer - the data to write
 * @param len - the number of bytes to write
 * @return the number of bytes written.
 * @return -1 on error
 **/
int ob_nvs_data_write(uint8_t domain, uint8_t id, void * buffer, int len);
/**
 * @brief deletes the data in the nvs partition
 * This function deletes all records in the nvs partition
 **/
void ob_nvs_data_factory_reset();
/**
 * @brief the definition of the callback when a data record is written
 * @param id - the ID of the data record
 * @param len - the number of bytes to write
 * @return the number of bytes written.
 **/
typedef void (*ob_nvs_mirror_callback_t)(uint8_t domain, uint8_t id, void * buffer, int len);

/**
 * @brief set a callback when data is written to nvs.
 **/
void  ob_nvs_set_mirror_callback(ob_nvs_mirror_callback_t callback);
#endif // CONFIG_ONBOARDING_NVS
