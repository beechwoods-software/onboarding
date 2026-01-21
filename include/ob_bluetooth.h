/*
 * Copyright 2024 (c) Beechwoods Software, Inc.
 *
 * All Rights Reserved
 */

#pragma once

#include <zephyr/sys_clock.h>

extern int bluetooth_init();

extern k_timeout_t SCAN_TIMEOUT;

/* Custom Service Variables */
/** @brief The UUID for onboarding is 8993c412-b941-4d54-8d09-c70b1d50b7a1 */
#define BT_UUID_CUSTOM_ONBOARDING_VAL                                   \
  BT_UUID_128_ENCODE(0x8993c412, 0xb941, 0x4d54, 0x8d09, 0xc70b1d50b7a1)

/** @brief The UUID for fetching the WiFi AP list charactaristic is d6cf98d9-1180-4e02-820b-d3de6ecf7206 */
#define BT_UUID_CUSTOM_GET_APS_VAL                                      \
  BT_UUID_128_ENCODE(0xd6cf98d9, 0x1180, 0x4e02, 0x820b, 0xd3de6ecf7206)

/** @brief The UUID for setting the WiFi AP charactaristic is 9c3a708e-2f6c-4336-8a68-4612a886dc81 */
#define BT_UUID_CUSTOM_SET_AP_VAL \
  BT_UUID_128_ENCODE(0x9c3a708e, 0x2f6c, 0x4336, 0x8a68, 0x4612a886dc81)


#define BLUETOOTH_LOG_MODULE_NAME bluetooth
#define BLUETOOTH_LOG_MODULE_LEVEL LOG_LEVEL_DBG


typedef struct obb_mode {
  int (*init)();
  int (*adv_start)();
  int (*adv_stop)();
  int (*scan_start)();
  int (*scan_stop)();
  int (*connect)();
  int (*connected)();
  int (*disconnect)();
  int (*disconnected)();
} obb_mode_t;

typedef enum  obb_mode_type {
  OBB_init, 
  OBB_adv_start,
  OBB_adv_stop,
  OBB_scan_start,
  OBB_scan_stop,
  OBB_connect,
  OBB_connected,
  OBB_disconnect,
  OBB_disconnected
} obb_mode_type_t;



