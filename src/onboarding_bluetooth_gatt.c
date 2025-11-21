/*
 * Copyright Beechwoods Software, Inc. 2024 Brad Kemp (brad@beechwoods.com)
 * All Rights Reserverd
 */

#ifdef CONFIG_ONBOARDING_BLUETOOTH_GATT

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/data/json.h>

#include <onboarding_bluetooth.h>
#include <onboarding_bluetooth_gatt.h>
#include <ob_wifi.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);
                   
K_SEM_DEFINE(scan_semaphore, 0, 1);

static const struct bt_uuid_128 primary_service_uuid = BT_UUID_INIT_128( BT_UUID_CUSTOM_ONBOARDING_VAL);

static const struct bt_uuid_128 read_aps_characteristic_uuid  = BT_UUID_INIT_128(BT_UUID_CUSTOM_GET_APS_VAL);

static const struct bt_uuid_128 write_current_ap_characteristic_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_SET_AP_VAL);

static ssize_t read_aps(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset);
static ssize_t read_current_ap(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset);
static ssize_t write_current_ap(struct bt_conn *conn,
					     const struct bt_gatt_attr *attr,
					     const void *buf, uint16_t len,
					     uint16_t offset, uint8_t flags);

static void aps_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
  LOG_DBG("aps_ccc_cfg_changed %d", value);

}

static void ob_join_network(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    char *ssid, char *passcode);
static void scan_and_update_list();
static void ob_update_ap_list(void);

struct ob_ap_list_entry {
  char *ssid;
  bool secure;
  uint32_t strength;
};

static const struct json_obj_descr secure_ap_list_entry_json_descr[] = {
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, ssid, JSON_TOK_STRING),
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, secure, JSON_TOK_TRUE),
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, strength, JSON_TOK_NUMBER),
};
static const struct json_obj_descr open_ap_list_entry_json_descr[] = {
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, ssid, JSON_TOK_STRING),
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, secure, JSON_TOK_FALSE),
  JSON_OBJ_DESCR_PRIM(struct ob_ap_list_entry, strength, JSON_TOK_NUMBER),
};


struct ob_current_ap {
  char *ssid;
  char *error;
};

static const struct json_obj_descr current_ap_set_json_descr[] = {
  JSON_OBJ_DESCR_PRIM(struct ob_current_ap, ssid, JSON_TOK_STRING),
};

struct ob_set_ap {
  char *ssid;
  char *passcode;
  char *error;
};

static const struct json_obj_descr set_ap_json_descr[] = {
  JSON_OBJ_DESCR_PRIM(struct ob_set_ap, ssid, JSON_TOK_STRING),
  JSON_OBJ_DESCR_PRIM(struct ob_set_ap, passcode, JSON_TOK_STRING),
  JSON_OBJ_DESCR_PRIM(struct ob_set_ap, error, JSON_TOK_STRING),
};

#define MAX_AP_LIST_LENGTH 64

static struct ob_ap_list_entry ap_list[MAX_AP_LIST_LENGTH]; 
static uint8_t ap_list_count = 0;

static char ap_list_data[2048] = {
  '[',
  ']'
};

static struct ob_current_ap current_ap;
static char current_ap_data[256] = {
	'{','"','s','s','i','d','"',':','"', '"',',',' ','"','e','r','r','o','r','"',':','"','"','}'
};

/* Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(primary_service,
	BT_GATT_PRIMARY_SERVICE(&primary_service_uuid),
              
	BT_GATT_CHARACTERISTIC(&read_aps_characteristic_uuid.uuid,
			        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			        BT_GATT_PERM_READ_ENCRYPT,
			        read_aps, NULL, ap_list_data),
        BT_GATT_CCC(aps_ccc_cfg_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

	BT_GATT_CHARACTERISTIC(&write_current_ap_characteristic_uuid.uuid,
			        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			        BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			        read_current_ap, write_current_ap, current_ap_data),
	BT_GATT_CCC(aps_ccc_cfg_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
);

static const struct bt_data advertisement[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_ONBOARDING_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static ssid_item_t * ssid_list;

void scan_complete(ssid_item_t * item)
{
  LOG_DBG("calling scan_complete");
  ssid_list = item;
  k_sem_give(&scan_semaphore);

}


static ssize_t read_current_ap(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
  LOG_DBG("READ CURRENT AP  -- current_ap_data = %s", current_ap_data);

  int rc = 0; // Number of bytes read

  const char *value = attr->user_data;
	rc = bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				                 strlen(current_ap_data));

  LOG_DBG("READ CURRENT AP (%d BYTES)", rc);
  return rc;
}

static ssize_t write_current_ap(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len,
				uint16_t offset, uint8_t flags)
{
  LOG_DBG("WRITE CURRENT AP (%d BYTES)", len);
  static char tmp_write_buffer[256];

  if (offset + len > sizeof(tmp_write_buffer)) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }
  
  memcpy(tmp_write_buffer + offset, buf, len);
  tmp_write_buffer[offset + len] = 0;
  
  if (flags | BT_GATT_WRITE_FLAG_EXECUTE | BT_GATT_WRITE_FLAG_CMD) {
    LOG_DBG("    JOIN THE NETWORK...");
    LOG_DBG("        JSON:  %s", tmp_write_buffer);

    // Parse the JSON
    struct ob_set_ap ap;
    int64_t result = json_obj_parse(tmp_write_buffer, strlen(tmp_write_buffer),
				    set_ap_json_descr, ARRAY_SIZE(set_ap_json_descr),
                                    &ap);

    if (result < 0) {
	  LOG_ERR("Failed to parse JSON");
  	  return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    
    LOG_DBG("Calling ob_join_network(ssid=%s, passcode=%s)\n", ap.ssid, ap.passcode);
    ob_join_network(conn, attr, ap.ssid, ap.passcode);

    LOG_DBG("WRITE CURRENT AP -- current_ap_data=%s", current_ap_data);
  }
  
  return len;
}

char uuid_str[BT_UUID_STR_LEN];

static ssize_t read_aps(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
  int rc = 0; // Number of bytes read
  
  LOG_DBG("READ AP LIST");

#ifdef CONFIG_ONBOARDING_WIFI

  // Only actually scan and update the list at the beginning
  if (offset == 0) {
	  scan_and_update_list();
  }
#endif // CONFIG_ONBOARDING_WIFI  

  const char *value = attr->user_data;

  LOG_DBG("Calling bt_gatt_attr_read(conn, attr, buf=0x%x, len=%d, offset=%d, value=0x%x, value_len=%d",
	  (uint32_t) buf, len, offset, (uint32_t) value, strlen(ap_list_data));
  rc = bt_gatt_attr_read(conn, attr, buf, len, offset, value,
			 strlen(ap_list_data));
  LOG_DBG("strlen(value) = %d", strlen(value));
  LOG_DBG("READ AP LIST (%d BYTES)", rc);

  LOG_DBG("read_aps");
  bt_uuid_to_str(attr->uuid,uuid_str, BT_UUID_STR_LEN);
  LOG_DBG("  len %d offset %d from %s", len, offset, uuid_str);

  return rc;
}


static int gatt_connected()
{
  int err = 0;
  
  return err;

}


int gatt_adv_stop()
{
  LOG_DBG("GATT adv_stop");
  bt_le_adv_stop();
  return 0;
}

int  gatt_adv_start()
{
  int err;
  LOG_DBG("GATT adv_start");

  err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                        advertisement, ARRAY_SIZE(advertisement),
                        sd, ARRAY_SIZE(sd));
  if (err) {
    LOG_ERR("Advertising failed to start (err %d)\n", err);
  }

  return err;
}

int gatt_init()
{
  LOG_DBG("GATT init");
  return 0;
}


struct obb_mode obb_mode_gatt = {
  .init = gatt_init,
  .adv_start = gatt_adv_start,
  .adv_stop = gatt_adv_stop,
  .connected = gatt_connected
};


static void ob_join_network(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    char *ssid, char *passcode)
{
  // For now, simply update the current_ap_data
  current_ap.ssid = ssid;
  current_ap.error = "";
  
  int result = json_obj_encode_buf(current_ap_set_json_descr, ARRAY_SIZE(current_ap_set_json_descr),
                                    &current_ap, current_ap_data, ARRAY_SIZE(current_ap_data));
  if (0 > result) {
    LOG_DBG("    PROBLEM ENCODING NEW AP TO JSON (err: %d)", result);
  }
  LOG_DBG("    CURRENT AP:  %s", current_ap_data);

  strcpy(gSSID, ssid);
  gSSID_len = strlen(gSSID);
  strcpy(gPSK, passcode);
  gPSK_len = strlen(gPSK);

  ob_wifi_connect();

  // DMR: Still need to write values to NVS
  
  bt_gatt_notify(conn, attr, current_ap_data, strlen(current_ap_data));
}

static void scan_and_update_list()
{
  int rc = 0;
  
  ob_wifi_scan();
  
  if((rc = k_sem_take(&scan_semaphore, SCAN_TIMEOUT)) < 0) {
    LOG_ERR("Scan timed out %d",rc);
  }

  ob_update_ap_list();

  LOG_DBG("ap_list_data=\"%s\"", ap_list_data);  

}	

static void ob_update_ap_list(void)
{
  LOG_DBG("UPDATE AP LIST CHRC");

  size_t max_size = ARRAY_SIZE(ap_list_data);
  size_t offset = 1;
  
  
  int ndx = 0;
  ssid_item_t *current_node = ssid_list;
  do
  {
    ap_list[ndx].ssid = current_node->ssid;
    ap_list[ndx].secure = current_node->security;
    ap_list[ndx].strength = current_node->signal_strength;

    if (++ndx == MAX_AP_LIST_LENGTH) {
	    LOG_ERR("ssid_list length exceeds MAX_AP_LIST_LENGTH");
	    break;
    }
    current_node = current_node->next;
  } while (current_node != NULL);
  ap_list_count=ndx;
  
  memset(ap_list_data, 0, max_size);
  ap_list_data[0] = '[';
  
  for (ndx = 0; ndx < ap_list_count; ++ndx) {
    if (ap_list[ndx].secure) {   
      json_obj_encode_buf(secure_ap_list_entry_json_descr,
                          ARRAY_SIZE(secure_ap_list_entry_json_descr),
                          &ap_list[ndx], ap_list_data + offset, max_size - offset);
    } else {
      json_obj_encode_buf(open_ap_list_entry_json_descr,
                          ARRAY_SIZE(open_ap_list_entry_json_descr),
                          &ap_list[ndx], ap_list_data + offset, max_size - offset);
    }

    if (ndx + 1 < ap_list_count) {
      ap_list_data[strlen(ap_list_data)] = ',';
    }
    offset = strlen(ap_list_data);
  }
  ap_list_data[strlen(ap_list_data)] = ']';

  LOG_DBG("APLIST:  %s", ap_list_data);
}

#endif // ONBOARDING_BLUETOOTH_GATT
