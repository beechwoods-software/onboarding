/*
 * Copyright 2023 Beechwoods Software Inc, Inc Brad Kemp
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
*/

#pragma once

#include <stdbool.h>

/**
 * @struct ssid_item
 * @brief a structre representing the discoverd SSIDs
 *
 * This structure is used in a linke list to hold all the SSIDs discovered
 * by a wifi scan
 */
struct ssid_item {
  /**
  * @var struct ssid_item * next
  * @brief pointer to the next ssid in the linked list
  */
  struct ssid_item * next;
  /**
   * @var char * ssid
   * @brief the name of the ssid
   */
  char * ssid;
  //  int lenf;
};
/**
 * @brief alias for a struct ssid_item
 */
typedef struct ssid_item ssid_item_t;

/** @brief the size of the APs IP address */
#define WIFI_AP_ADDRESS_SIZE 16

/** @brief string of the IP address to use for the AP */
extern char  wifi_ap_address[];

/** @brief the nvs domain id for WIFI */
#define NVS_DOMAIN_WIFI      1
/** @brief the data record identifier for the SSID to connect with */
#define NVS_ID_WIFI_SSID     0
/** @brief the data record identifier for the PSK of the SSID to connect with */
#define NVS_ID_WIFI_PSK      1
/** @brief the data record identifier for the host name of the device */
#define NVS_ID_WIFI_HOSTNAME 2
/** @brief the data record identifier for the end of the data record identifiers */
#define NVS_ID_WIFI_SENTINAL 3

/**
 * @brief signature for the callback when a wifi scan is completed
 *
 * @param it pointer to the linked list of ssid_item_t items found
 */ 
typedef void(*scan_done_callback_t)(ssid_item_t * it);
/**
 * @brief signature for the callback when an IPV4 address is acquired
 */
typedef void(*address_add_callback_t)(void);

/**
 * @brief set the callback address to be called when an IPV4 address is acquired
 *
 * @param callback the address of the callback function
 */
void set_address_add_callback(address_add_callback_t callback);
/**
 * @brief set the callback address to be called when a wifi scan completes.
 *
 * @param callback the address of the callback function
 */
void set_scan_done_callback(scan_done_callback_t callback);
/**
 * @brief conntect wifi to an access point
 *
 * @note the SSID and PSK for the target station are stored in ob_nvs_data
 */
int ob_wifi_connect(void);
/**
 *@brief scan for reachable APs
 * It scans for reachable APS and creates a linked list of the SSIDs found.
 * When the scan completes if the scan_done_callback is set that function is called
 * @return 0 on success
 * @return -1 on error
 */
int ob_wifi_scan(void);
/**
 *@brief enable a wifi AP
 *
 * @note the ssid and psk for the AP are held in NVS storage
 */
void ob_wifi_ap_enable(void);
/**
 * @brief disable the wifi AP
 */
void ob_wifi_ap_disable(void);
/**
 * @brief initializ the wifi
 */
int ob_wifi_init(void);
/**
 *@brief deinitialize the wifi
 * this will bring down a connection and/or an AP
 */
void ob_wifi_deinit(void);
/**
 * @brief returns th state of an AP
 * @return true if AP active
 * @return false if AP inactive
 */
bool ob_wifi_HasAP(void);


//extern struct k_work_delayable start_ap_work;
#define AP_WORK_DELAY K_MSEC(500)

/**
 * @brief convert a MAC address to a C string
 * @details not thread safe returns pointer to static buffer
 *
 * @param macp Pointer to MAC address
 * @return Pointer to character buffer with MAC address as a string
 */
char * Mac2String(uint8_t * macp);

/**
 * @brief get the MAC address of the devices AP interface
 *
 * @param buffer The buffer to hold the MAC address.
 * @param len The lenght of the buffer.
 *
 * @result return true if successful false on failure
 */
bool get_mac_address(uint8_t * buffer, int len);
