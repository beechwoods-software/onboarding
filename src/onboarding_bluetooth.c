
/*
 * Copyright Beechwoods Software, Inc. 2023 brad@beechwoods.com
 * All Rights Reserved
 */

#ifdef CONFIG_ONBOARDING_BLUETOOTH


#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include "onboarding_bluetooth.h"
#include "onboarding_bluetooth_gatt.h"
#include "ob_wifi.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

/**
 * @brief BLE service states
 */
static enum {
  BLE_DISCONNECTED,    // The connection has disconneced
  BLE_SCAN_START,    // A scan is requested
  BLE_SCAN_STOP,          // 
  BLE_CONNECT_CREATE,
  BLE_CONNECT_CANCEL,
  BLE_ADV_START,
  BLE_ADV_STOP,
  BLE_CONNECTED,
  BLE_WAITING
} ble_state;

static struct k_work_delayable ble_work;

k_timeout_t SCAN_TIMEOUT = K_MSEC(5000);

obb_mode_t *obb_modes[] = {
#ifdef CONFIG_ONBOARDING_BLUETOOTH_BLE
  &obb_mode_ble ,
#endif // CONFIG_ONBOARDING_BLE
#ifdef CONFIG_ONBOARDING_BLUETOOTH_GATT
  &obb_mode_gatt,
#endif // CONFIG_ONBOARDING_BLUETOOTH_GATT
  NULL
};

#define OBB_MAX  4

#ifndef CONFIG_ONBOARDING_BLUETOOTH_GATT

static k_timeout_t adv_timeout(void)
{
  uint32_t timeout;

#if 1
  return K_FOREVER;
#else
  if (bt_rand(&timeout, sizeof(timeout)) < 0) {
    return 10 * MSEC_PER_SEC;
  }
  
  timeout %= (10 * MSEC_PER_SEC);
  
  return K_MSEC(timeout + (1 * MSEC_PER_SEC));
#endif
}
#endif //!CONFIG_ONBOARDING_BLUETOOTH_GATT

void foreach_obb(obb_mode_type_t type)
{
  
  obb_mode_t *cb;
  LOG_DBG("foreach %d", type);
  for( int i = 0; i < OBB_MAX; i++)  { 
    cb = obb_modes[i];
    if ( NULL != cb) { 
      switch(type) {
      case OBB_init:
        if(NULL != cb->init) {
          cb->init();
        }
        break;
      case OBB_adv_start:
        if(NULL != cb->adv_start) {
          cb->adv_start();
        }
        break;
      case OBB_adv_stop:
        if( NULL != cb->adv_stop) {
          cb->adv_stop();
        }
        break;
      case OBB_scan_start:
        if(NULL != cb->scan_start) {
          cb->scan_start();
        }
        break;
      case OBB_scan_stop:
        if(NULL != cb->scan_stop) {
          cb->scan_stop();
        }
        break;
      case OBB_connect:
        if(NULL != cb->connect) {
          cb->connect();
        }
        break;
      case OBB_connected:
        if(NULL != cb->connected) {
          cb->connected();
        }
        break;
      case OBB_disconnect:
        if(NULL != cb->disconnect) {
          cb->disconnect();
        }
        break;
      case OBB_disconnected:
        if(NULL != cb->disconnected) {
          cb->disconnected();
        }
        break;
      }
    } else {
      LOG_DBG("End found");
      break;
    }
  }
}
#define FOREACH_OBB(name) foreach_obb(OBB_##name)
/**
 * @brief The core of the BLE state machine
 */
static void ble_state_machine(struct k_work *work)
{
  LOG_DBG("ble state_machine  %d", ble_state);
  switch (ble_state) {
  case BLE_DISCONNECTED:
    LOG_DBG("Disconnected");	  
    FOREACH_OBB(disconnected);
    break;
  case BLE_SCAN_START:
    FOREACH_OBB(scan_start);
    LOG_DBG("Started scanning");
    ble_state = BLE_SCAN_STOP;
    k_work_reschedule(&ble_work, SCAN_TIMEOUT);
    break;
  case BLE_CONNECT_CREATE:
    LOG_WRN("Connection attempt timed out");
    FOREACH_OBB(disconnect);
    FOREACH_OBB(disconnected);
    ble_state = BLE_ADV_START;
    k_work_reschedule(&ble_work, K_NO_WAIT);
    break;
  case BLE_SCAN_STOP:
    LOG_WRN("No devices found during scan\n");
    FOREACH_OBB(scan_stop);
    ble_state = BLE_ADV_START;
    k_work_reschedule(&ble_work, K_NO_WAIT);
    break;
  case BLE_ADV_START:
    LOG_DBG("Advertising started");
    FOREACH_OBB(adv_start);
    ble_state = BLE_WAITING;
    k_work_reschedule(&ble_work, K_NO_WAIT);
    break;
  case BLE_ADV_STOP:
    LOG_WRN("Advertising stop");
    FOREACH_OBB(adv_stop);
    ble_state = BLE_ADV_START; // Change to BLE_SCAN_START to interleave scannigng
    k_work_reschedule(&ble_work, K_NO_WAIT);
    break;
  case BLE_CONNECTED:
    LOG_DBG("Connected");
    FOREACH_OBB(connected);
  case BLE_CONNECT_CANCEL:
    break;
  case BLE_WAITING:
    break;
  }
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err)
	{
		LOG_DBG("Security changed: level %u", level);
	}
	else
	{
		LOG_DBG("Security failed: level %u err %d\n", level, err);
	}
}

// Callback for BLE update request
static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_DBG("***************************** le_param_req() called");
	// If acceptable params, return true, otherwise return false.
	return true;
}

// Callback for BLE parameter update
static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];

	if (bt_conn_get_info(conn, &info))
	{
		printk("****************************** Could not parse connection info\n");
	}
	else
	{
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		printk("************************** Connection parameters updated!	\n\
		Connected to: %s						\n\
		New Connection Interval: %u				\n\
		New Slave Latency: %u					\n\
		New Connection Supervisory Timeout: %u	\n",
			   addr, info.le.interval, info.le.latency, info.le.timeout);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
  struct bt_conn_info info;
  
  if (err) {
    LOG_ERR("Connection failed (err 0x%02x)", err);
    return;
  }
  
  bt_conn_get_info(conn, &info);
  LOG_DBG("type 0x%x role 0x%x id 0x%x state %d", info.type, info.role, info.id, info.state);
  LOG_DBG("Connected");
  return;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
  LOG_DBG("Disconnected (reason 0x%02x)", reason);
  ble_state = BLE_ADV_START;
  k_work_reschedule(&ble_work, K_NO_WAIT);

}

static void recycled()
{
  LOG_DBG("recycled");
}

static void identity_resolved_cb(struct bt_conn *conn, const bt_addr_le_t *rpa, const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

        LOG_DBG("******************************************* Identity resolved %s -> %s", addr_rpa, addr_identity);

	return;
}


static struct bt_conn_cb conn_callbacks =
{
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated,
	.recycled = recycled,
	.identity_resolved = identity_resolved_cb,
	
};


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("Bluetooth Pairing cancelled: %s", addr);
}

static void auth_pairing_confirm_cb(struct bt_conn *conn)
{
	LOG_DBG("******************************************* Bluetooth Pairing confirmed");
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = NULL, // Needs to be NULL or PIN will be required
	.passkey_confirm = NULL, 
	.cancel = auth_cancel,
	.pairing_confirm = auth_pairing_confirm_cb,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Bluetooth Pairing Complete");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Bluetooth Pairing Failed (%d). Disconnecting.", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_cb_info = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void bt_enable_callback(int rc)
{

  LOG_DBG("bt_enable_callback");
  if (rc) {
    LOG_ERR("Bluetooth init failed:  %d", rc);
    return;
  }
  FOREACH_OBB(init);

  /* Configure pairing callbacks */
  // Enable pairing without MITM (no passkey)
  bt_conn_cb_register(&conn_callbacks);
  bt_conn_auth_cb_register(&conn_auth_callbacks);
  bt_conn_auth_info_cb_register(&auth_cb_info);

  settings_load();
  
  LOG_DBG("Bluetooth initialized");
}
#endif // CONFIG_ONBOARDING_BLUETOOTH

/**
 * @brief bluetooth_init() is the entry point that is called from the application using bluetooth onboarding
 */
int bluetooth_init() 
{
  int rc = 0;
  LOG_DBG("Calling bluetooth_init()");
#ifdef CONFIG_ONBOARDING_BLUETOOTH
#ifdef CONFIG_ONBOARDING_WIFI
  set_scan_done_callback(&scan_complete);
#endif // CONFIG_ONBOARDING_WIFI
  
  rc = bt_enable(bt_enable_callback);
  if(0 != rc) {
    LOG_ERR("bt_enabled failed %d",rc);
  } else {
    k_work_init_delayable(&ble_work, ble_state_machine);
    LOG_DBG("Starting ble state machine");
    ble_state = BLE_ADV_START;
    k_work_reschedule(&ble_work, K_NO_WAIT);
  }
#else // CONFIG_ONBOARDING_BLUETOOTH
#warning "bluetooth not configured"
  rc = -EINVAL;
#endif // CONFIG_ONBOARDING_BLUETOOTH
  return rc;
}
