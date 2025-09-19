/*
 * Copyright 2023 2024 Beechwoods Software, Inc Brad Kemp
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
 */

#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/ipv4_autoconf.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#ifdef CONFIG_WIFI_NM
#include <zephyr/net/wifi_nm.h>
#endif


#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/ethernet_mgmt.h>

#include "ob_wifi.h"
#include "ob_nvs_data.h"
#ifdef CONFIG_USE_READY_LED
#include <ready_led.h>
#endif

LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

/** @brief the callback event structure for wifi events */
static struct net_mgmt_event_callback wifi_mgmt_cb;
/** @brief the callback event structure for ipv4 events */
static struct net_mgmt_event_callback ipv4_mgmt_cb;
/** @brief the callback event strucure for ehternet events */
static struct net_mgmt_event_callback ethernet_mgmt_cb;
/** @brief indicates that the wifi module hs been initialized */
static bool wifi_inited = false;

/** @brief a semaphore released after a wifi connection completes */
static K_SEM_DEFINE(wifi_connect_sem, 0, 1);
/** @brief a sempaphore released when the AP is disabled
    This usually means that the SSID and PSK of the target wifi have been configured
*/
static K_SEM_DEFINE(wifi_ap_sem, 0, 1);
/** @brief a semaphore released when the wifi has been disonnected during wifi shutdown */
static K_SEM_DEFINE(wifi_deinit_sem, 0, 1);

/** @brief the head of the SSID list */
ssid_item_t *ssid_head = NULL;
/** @brief the tail of the SSID list */
ssid_item_t *ssid_tail = NULL;

/** @brief indicates if the device AP isactive */
bool mHasAp = false;

/** @brief the SSID to connect with */
static char gSSID[WIFI_SSID_MAX_LEN];
/** @brief the length of the connect SSID */
static int gSSID_len;
/** @brief the PSK for the connect SSID */
static char gPSK[WIFI_PSK_MAX_LEN];
/** @brief ths length of the PSK for the connect SSID */
static int gPSK_len;

#ifdef CONFIG_ONBOARDING_WIFI_AP
#ifdef CONFIG_NET_DHCPV4_SERVER
/** @brief the network mask for the devices DHCP server */
static struct in_addr netmask = { { { 255, 255, 255, 0 } } };
#endif
/** @brief the ssid of the device AP */
char wifi_ap_ssid[WIFI_SSID_MAX_LEN];
/** @brief the PSK of the device AP */
char wifi_ap_psk[WIFI_PSK_MAX_LEN];
/** @brief the IP address for the devices AP */
char wifi_ap_address[WIFI_AP_ADDRESS_SIZE];

/** @brief The delayed work structure for the devices AP */
struct k_work_delayable start_ap_work;

static void _ob_wifi_ap_enable(void);

/**
 * @brief This function is called from the delayed work structure to enable the device AP
 * @param work The delayed work structure
 */
static void bws_start_ap_work(struct k_work * work)
{
   _ob_wifi_ap_enable();
}
#endif // CONFIG_ONBOARDING_WIFI_AP

/** @brief callback called when a wifi scan is completed */
scan_done_callback_t done_callback = NULL;
/** @brief callback called when an IPV4 address is added  */
address_add_callback_t address_add_callback = NULL;

char *
Mac2String(uint8_t * macp)
{
  static char macbuf[18];
  snprintf(macbuf, sizeof(macbuf),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           macp[0], macp[1], macp[2], macp[3], macp[4], macp[5]);
  return macbuf;
}

bool
get_mac_address(uint8_t * buffer, int len)
{
  struct net_if *iface = NULL;
  struct net_linkaddr * linkaddr;
  bool rc = false;
  iface = net_if_get_wifi_sap();
  LOG_DBG("mac iface %s", iface->config.name);
  if(NULL != iface) {
    linkaddr = net_if_get_link_addr(iface);
    if(len >= 6) {
      memcpy(buffer, linkaddr->addr, 6);
      rc = true;
    } else {
      LOG_ERR("len < %d : %d", 6, len);
    }
  } else {
    LOG_ERR("get_mac_address:: Unable to get iface");
  }

  return rc;
}


bool ob_wifi_HasAP(void)
{
  LOG_DBG("is AP %s", mHasAp?"True":"False");
  return mHasAp;
}

void set_scan_done_callback(scan_done_callback_t func)
{
  done_callback = func;
}

void set_address_add_callback(address_add_callback_t callback)
{
  address_add_callback = callback;
}

/**
 * @brief free an item from the list of SSIDs
 * @details this recursively frees all subsequent items in the SSID list
 * @param it  Pointer to ssid item to free
 */
static void
ssid_free_item(ssid_item_t * it)
{
  if(NULL == it) {
    return;
  }
  ssid_free_item(it->next);
  free(it->ssid);
  free(it);
}

/**
 * @brief Add an SSID item to the ssid list
 *
 * @param ssid Pointer to the SSID name
 * @param ssid_length lenght of the SSID name
 */
static void
ssid_add_item(const char * ssid, int ssid_length)
{
  ssid_item_t * it = NULL;
  if(NULL != ssid_head) {
    for(it = ssid_head; NULL != it; it = it->next) {
      if(0 == strncmp(ssid, it->ssid, ssid_length)) {
        break;
      }
    }
  }
  if(NULL == it) {
    it = malloc(sizeof(ssid_item_t));
    //    LOG_DBG("Malloc %d", (int) sizeof(ssid_item_t));
    if(NULL == it) {
      LOG_ERR("Mem alloc failed");
      return;
    }
    it->next = NULL;
    it->ssid = malloc(ssid_length +1);
    LOG_DBG("ssid malloc %d %s", ssid_length +1 , ssid);
    if(NULL == it->ssid) {
      LOG_ERR("SSID alloc failed %d", ssid_length);
      free(it);
      return;
    }
    strncpy(it->ssid, ssid, ssid_length);
    it->ssid[ssid_length] = '\0';
    //    it->len = ssid_length;
    if(NULL == ssid_head) {
      ssid_head = it;
      ssid_tail = it;
    } else {
      ssid_tail->next = it;
      ssid_tail = it;
    }
  }
}
/**
 * @brief initialize the list of SSIDs
 * @details the previous list is erased
 */
void
ssid_init_list(void)
{
  ssid_free_item(ssid_head);
  ssid_head = NULL;
  ssid_tail = NULL;
}

/**
 * @brief call back for IPV4 management events
 *
 * @param cb pointer to the network management event callback
 * @param mgmt_event the management event
 * @param iface the interface that genreated the event
 */
static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event, struct net_if *iface)
{
  const struct wifi_status *status = (const struct wifi_status *) cb->info;
  switch (mgmt_event) {
  case NET_EVENT_IPV4_DHCP_BOUND:
    if (status->status) {
      LOG_ERR("DHCP  request failed (%d)(%d:%d:%d)", status->status, status->conn_status, status->disconn_reason, status->ap_status);
    } else {
      LOG_INF("DHCP bound");
      k_sem_give(&wifi_connect_sem);

    }
    break;

  case NET_EVENT_IPV4_DHCP_START:
    LOG_DBG("DHCP started");
    break;

  case NET_EVENT_IPV4_DHCP_STOP:
    LOG_WRN("DHCP Stopped");
    break;

  case NET_EVENT_IPV4_ADDR_ADD:
    {
      const struct in_addr * in = (const struct in_addr *)cb->info;
      char buffer[20];

      zsock_inet_ntop(AF_INET, in, buffer, sizeof(struct in_addr));
      LOG_DBG("Address add (%s)", buffer);
      if(NULL != address_add_callback) {
        (*address_add_callback)();
      }
    }
    break;

  case NET_EVENT_IPV4_ADDR_DEL:
    {
      const struct in_addr * in = (const struct in_addr *)cb->info;
      char buffer[20];
      zsock_inet_ntop(AF_INET, in, buffer, sizeof(struct in_addr));
      LOG_ERR("Address delete (%s)", buffer);
    }
    break;

  case NET_EVENT_IPV4_MCAST_JOIN:
    LOG_ERR("IPV4 MCAST JOIN");
    break;

  default:
    LOG_ERR("Unhandled IPV4 mgmt event 0x%llx", mgmt_event);
    break;
  }
}

/**
 * @brief Callback for ethernet events
 *
 * @param cb The network management callback structure
 * @param mgmt_event The event
 * @param iface The inteface which generated the event
 */
static void ethernet_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event, struct net_if *iface)
{
  switch (mgmt_event) {
  case NET_EVENT_ETHERNET_CARRIER_ON:
    LOG_INF("Ethernet carrier on");
    break;
  case NET_EVENT_ETHERNET_CARRIER_OFF:
    LOG_INF("Ethernet carrier off");
    break;
  default:
    LOG_ERR("Unhandled ethernet mgmt event 0x%llx", mgmt_event);
    break;
  }
}

/**
 * @brief Callback for wifi network management events
 *
 * @param cb The network management callback structure
 * @param mgmt_event The event
 * @param iface The inteface which generated the event
 */
static void ob_wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event, struct net_if *iface)
{
  LOG_DBG("Got event 0x%llx", mgmt_event);
  const struct wifi_status *status =
		(const struct wifi_status *)cb->info;
  const struct wifi_scan_result *entry =
    (const struct wifi_scan_result *)cb->info;
  switch (mgmt_event) {
  case NET_EVENT_WIFI_SCAN_RESULT:
    ssid_add_item(entry->ssid, entry->ssid_length);
    break;

  case NET_EVENT_WIFI_SCAN_DONE:
    LOG_DBG("Wifi scan done");
    if(NULL != done_callback) {
      (*done_callback)(ssid_head);
    }
    ssid_init_list();
    break;

  case NET_EVENT_WIFI_IFACE_STATUS:
    LOG_INF("Iface status for %s", ((const struct wifi_iface_status *)(cb->info))->ssid);
    break;

  case NET_EVENT_WIFI_CONNECT_RESULT:
    LOG_DBG("Wifi Connect result %s", iface->config.name);

    if (status->status) {
      LOG_ERR("Connect result request failed (%d)(%d:%d:%d)", status->status, status->conn_status, status->disconn_reason, status->ap_status);
    } else {
      LOG_INF("WIFI Connected");
#ifndef CONFIG_ESP32_STA_AUTO_DHCP
      net_dhcpv4_start(iface);
#endif
      // esp32 waits until dhcp bound
      //    k_sem_give(&wifi_connect_sem);
    }
    break;

  case NET_EVENT_WIFI_DISCONNECT_RESULT:
    LOG_DBG("Wifi Disonnect result status  0x%x conn_status 0x%x reason 0x%x wifi_status 0x%x", status->status, status->conn_status, status->disconn_reason, status->ap_status);
#ifdef CONFIG_USE_READY_LED
    ready_led_color(255,0,0);
    ready_led_set(READY_LED_PANIC);
#endif
    break;

  case NET_EVENT_WIFI_DISCONNECT_COMPLETE:
    LOG_DBG("Wifi Disonnect Complete");
    break;

  case NET_EVENT_WIFI_AP_ENABLE_RESULT:
	if (status->status) {
		LOG_WRN("AP enable request failed (%d)\n", status->status);
	} else {
		LOG_DBG("AP enabled");
	}
    break;

  case NET_EVENT_WIFI_AP_DISABLE_RESULT:
	if (status->status) {
		LOG_WRN("AP disable request failed (%d)\n", status->status);
	} else {
		LOG_DBG("AP disabled\n");
        k_sem_give(&wifi_deinit_sem);
	}
    break;

  case NET_EVENT_IPV4_DHCP_START:
    LOG_INF("wifi DHCP Start");
    break;

  case NET_EVENT_IPV4_DHCP_STOP:
    LOG_INF("wifi DHCP Stop");
    break;

  case NET_EVENT_WIFI_AP_STA_CONNECTED:
    LOG_INF("STA connected to AP");
    break;

  case NET_EVENT_WIFI_AP_STA_DISCONNECTED:
    LOG_INF("STA disconnected from AP");
    break;

  default:
    LOG_ERR("Unhandled wifi mgmt event 0x%llx", mgmt_event);
    break;
  }
}
/** @brief a bit mask of the wifi management events to recieve */
#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_SCAN_RESULT         | \
                          NET_EVENT_WIFI_SCAN_DONE           | \
                          NET_EVENT_WIFI_IFACE_STATUS        | \
                          NET_EVENT_WIFI_TWT                 | \
                          NET_EVENT_WIFI_RAW_SCAN_RESULT     | \
                          NET_EVENT_WIFI_CONNECT_RESULT      | \
                          NET_EVENT_WIFI_DISCONNECT_RESULT   | \
                          NET_EVENT_WIFI_DISCONNECT_COMPLETE | \
                          NET_EVENT_WIFI_AP_ENABLE_RESULT    | \
                          NET_EVENT_WIFI_AP_DISABLE_RESULT   | \
                          NET_EVENT_WIFI_AP_STA_CONNECTED    | \
                          NET_EVENT_WIFI_AP_STA_DISCONNECTED )

/** @brief a bit mask of the IPV4 manangement events to recieve */
#define IPV4_MGMT_EVENTS ( \
                           NET_EVENT_IPV4_DHCP_BOUND | \
                           NET_EVENT_IPV4_ADDR_ADD   | \
                           NET_EVENT_IPV4_ADDR_DEL)

/** @brief a bit mask of Ethernet events to recieve */
#define ETHERNET_MGMT_EVENTS ( \
                          NET_EVENT_ETHERNET_CARRIER_ON | \
                          NET_EVENT_ETHERNET_CARRIER_OFF)

int
ob_wifi_init(void)
{
#ifdef CONFIG_ONBOARDING_WIFI_AP
  char mac_addr_buf[18];
  char * mad = NULL;
  char ssid_suffix[7];
#endif // CONFIG_ONBOARDING_WIFI_AP

#ifdef CONFIG_NET_HOSTNAME_DYNAMIC
  int len;
  char hostname[NET_HOSTNAME_MAX_LEN];
#endif

  if(ob_nvs_data_init() < 0) {
    return -1;
  }
  if(ob_nvs_data_register_ids(NVS_DOMAIN_WIFI, NVS_ID_WIFI_SENTINAL) < 0) {
    LOG_ERR("Wifi Unable to register nvs ids");
    return -1;
  }

#ifdef CONFIG_NET_HOSTNAME_DYNAMIC
  /* Only read the hostname if the app has set dynamic hostnames */
  if((len = ob_nvs_data_read(NVS_DOMAIN_WIFI, NVS_ID_WIFI_HOSTNAME, hostname, sizeof(hostname))) < 0) {
    LOG_WRN("Unable to read hostname %d setting to %s", len, net_hostname_get());
  } else {
    len = net_hostname_set(hostname, strlen(hostname));
    if(len < 0) {
      LOG_ERR("Setting hostname to %s failed %d", hostname, len);
    } else {
      LOG_DBG("Hostname set to %s", hostname);
    }
  }
#endif

  net_mgmt_init_event_callback(&wifi_mgmt_cb,
                               ob_wifi_mgmt_event_handler,
                               WIFI_MGMT_EVENTS);

  net_mgmt_add_event_callback(&wifi_mgmt_cb);
  net_mgmt_init_event_callback(&ipv4_mgmt_cb,
                               ipv4_mgmt_event_handler,
                               IPV4_MGMT_EVENTS);

  net_mgmt_add_event_callback(&ipv4_mgmt_cb);

  net_mgmt_init_event_callback(&ethernet_mgmt_cb,
                               ethernet_mgmt_event_handler,
                               ETHERNET_MGMT_EVENTS);
  net_mgmt_add_event_callback(&ethernet_mgmt_cb);
#ifdef CONFIG_ONBOARDING_WIFI_AP
#ifndef CONFIG_ONBOARDING_WIFI_AP_ADDRESS
  #error "No AP IP Address configured"
#endif
  strcpy(wifi_ap_address, CONFIG_ONBOARDING_WIFI_AP_ADDRESS);
  // Need to append the last three octets of the mac address
  strcpy(wifi_ap_ssid, CONFIG_ONBOARDING_WIFI_AP_SSID);
  if(get_mac_address(mac_addr_buf, sizeof(mac_addr_buf))){
    mad = Mac2String(mac_addr_buf);
    if(NULL != mad) {
      ssid_suffix[0] = mad[9];
      ssid_suffix[1] = mad[10];
      ssid_suffix[2] = mad[12];
      ssid_suffix[3] = mad[13];
      ssid_suffix[4] = mad[15];
      ssid_suffix[5] = mad[16];
      ssid_suffix[6] = '\0';
      strcat(wifi_ap_ssid, ssid_suffix);
    }
  }
  LOG_DBG("got mac %s", wifi_ap_ssid);
  strcpy(wifi_ap_psk, CONFIG_ONBOARDING_WIFI_AP_PSK);


  k_work_init_delayable(&start_ap_work,bws_start_ap_work);
#endif // CONFIG_ONBOARDING_WIFI_AP

  wifi_inited=true;
#ifdef CONFIG_ONBOARDING_WIFI_AP
  bool done = false;
  bool isAP;
  while(!done) {
    isAP = false;
    if((gSSID_len = ob_nvs_data_read(NVS_DOMAIN_WIFI, NVS_ID_WIFI_SSID, gSSID, sizeof(gSSID))) < 0) {
      LOG_ERR("Unable to read SSID");
#ifdef CONFIG_ONBOARDING_PRECONFIG_WIFI
      strncpy(gSSID, CONFIG_ONBOARDING_WIFI_SSID, WIFI_SSID_MAX_LEN);
      gSSID_len =strlen(gSSID);
      LOG_ERR("Setting SSID to %s", gSSID);
#else
      isAP = true;
#endif // CONFIG_ONBOARDING_PRECONFIG_WIFI
    }

    if((gPSK_len = ob_nvs_data_read(NVS_DOMAIN_WIFI, NVS_ID_WIFI_PSK, gPSK, sizeof(gPSK))) < 0) {
      LOG_ERR("Unable to read PSK");
#ifdef CONFIG_ONBOARDING_PRECONFIG_WIFI
      strncpy(gPSK, CONFIG_ONBOARDING_WIFI_PSK, WIFI_PSK_MAX_LEN);
      gPSK_len = strlen(gPSK);
#else
      isAP = true;
#endif // CONFIG_ONBOARDING_RECONFIG_WIFI
    }
    if(isAP) {
      // Start the AP
      k_work_schedule(&start_ap_work, AP_WORK_DELAY);
      LOG_DBG("waiting on AP");
      k_sem_take(&wifi_ap_sem, K_FOREVER);
      LOG_DBG("Ready to try reading address again");
      done = true;
    } else {
      LOG_DBG("Connecting");
      if(ob_wifi_connect()< 0) {
        ob_wifi_ap_enable();
      }
      done = true;

    }
  }
#else // CONFIG_ONBOARDING_WIFI_AP
  LOG_DBG("Connecting");
  ob_wifi_connect();
#endif // CONFIG_ONBOARDING_WIFI_AP

  LOG_DBG("Wifi inited");
  return 0;
}

void
ob_wifi_deinit(void)
{
  int rc = 0;
  struct net_if *iface;
  LOG_DBG("Wifi deinit");
  k_sem_reset(&wifi_deinit_sem);
#ifdef CONFIG_ONBOARDING_WIFI_AP
  if(ob_wifi_HasAP()) {
    ob_wifi_ap_disable();
  }
#endif // CONFIG_ONBOARDING_WIFI_AP
  iface = net_if_get_wifi_sta();
  rc = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
  if(rc < 0) {
    LOG_ERR("Wifi deinitialization failed %d", rc);
  }
  k_sem_take(&wifi_deinit_sem, K_MSEC(5000));
  wifi_inited=false;
  LOG_INF("Wifi deinited");
}

int
ob_wifi_scan(void)
{
  struct net_if *iface = net_if_get_wifi_sta();
  LOG_DBG("scan iface %s", iface?iface->config.name:"NULL");
  // TODO why?
  if(!wifi_inited) {
    LOG_ERR("Wifi not initied");
    return -1;
  }
  ssid_init_list();
  LOG_DBG("Scan started");
  if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0)) {
    LOG_ERR("Wifi scan faild");
  }
  return 0;
}
#ifdef CONFIG_ONBOARDING_WIFI_AP
void
ob_wifi_ap_disable(void)
{
  int rc;
  struct net_if *iface = net_if_get_wifi_sap();
  LOG_DBG("ap disable iface %p", iface);
#ifdef CONFIG_NET_DHCPV4_SERVER
  struct in_addr addr;
  if (net_addr_pton(AF_INET, wifi_ap_address, &addr)) {

      NET_ERR("Invalid address: %s", wifi_ap_address);
    return;
  }
  LOG_INF("remove IP addr  %s", wifi_ap_address);
  if(!net_if_ipv4_addr_rm(iface, &addr))  {
    LOG_ERR("net_if_ipv4_addr_add failed %d", errno);
  }

  rc = net_dhcpv4_server_stop(iface);
  if(rc < 0) {
    LOG_ERR("Unable to stop dhcp server %d", rc);
  }

#endif // CONFIG_NET_DHCPV4_SERVER

  rc = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
  if (rc < 0) {
    LOG_ERR("AP mode disable failed %s", strerror(errno));
  } else {
    k_sem_give(&wifi_deinit_sem);
    k_sem_give(&wifi_ap_sem);
  }
}
void ob_wifi_ap_enable()
{
    k_work_schedule(&start_ap_work, AP_WORK_DELAY);
}

/**
 * @brief This function enables the devices AP
 */
static void
_ob_wifi_ap_enable(void)
{
  int rc;
  struct net_if *iface = net_if_get_wifi_sap();
  LOG_DBG("ap enable iface %s", iface?iface->config.name:"NULL");
#ifdef CONFIG_USE_READY_LED
  ready_led_color(0, 0, 255);
  ready_led_set(READY_LED_SHORT);
#endif
  if(NULL == iface) {
    LOG_ERR("Wifi Interface not found");
    return;
  }
#ifdef CONFIG_NET_DHCPV4_SERVER
  struct in_addr addr;
  if (net_addr_pton(AF_INET, wifi_ap_address, &addr)) {
    NET_ERR("Invalid address: %s", wifi_ap_address);
    return;
  }
  LOG_INF("Set IP addr to %s", wifi_ap_address);
  if(NULL == net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0))  {
    LOG_ERR("net_if_ipv4_addr_add failed %d", errno);
    return;
  }

  if(!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
    LOG_ERR("Unable to set netask for address %s\n", wifi_ap_address);
    return;
  }

  /* Set the starting address for the dhcp server pool */
  addr.s4_addr[3]++;

#endif // CONFIG_NET_DHCPV4_SERVER

  struct wifi_connect_req_params ap_cnx_params;
  struct wifi_connect_req_params *params = &ap_cnx_params;
  memset(params, 0, sizeof(ap_cnx_params));

  /* Defaults */
  params->security = WIFI_SECURITY_TYPE_PSK;
  params->band = WIFI_FREQ_BAND_UNKNOWN;
  params->channel = WIFI_CHANNEL_ANY;
  params->mfp = WIFI_MFP_OPTIONAL;

  //  params->band =  WIFI_FREQ_BAND_2_4_GHZ; //WIFI_FREQ_BAND_UNKNOWN;
  //  params->channel = 6; // WIFI_CHANNEL_ANY;

  /* SSID */
  params->ssid = wifi_ap_ssid;
  params->ssid_length = strlen(params->ssid);

  /* PSK (optional) */
  params->psk = wifi_ap_psk;
  params->psk_length = strlen(params->psk);

  //  params->mfp = WIFI_MFP_OPTIONAL;
  LOG_INF("ssid(len) = %s(%d)", params->ssid, params->ssid_length);
  LOG_INF("PSK(len) %s(%d) type %d", params->psk, params->psk_length, params->security);
  //  LOG_INF("sae(len) %s(%d)",params->sae_password, params->sae_password_length);
  LOG_INF("band %d channel %d security %d mfp %d", params->band, params->channel, params->security, params->mfp);
#if 1
  LOG_INF("SSID %s timout %d", params->ssid, params->timeout);
#else
  LOG_INF("SSID 0x%02x%02x%02x%02x%02x%02x timeout %d",params->ssid[0], params->ssid[1],
          params->ssid[2], params->ssid[3], params->ssid[4], params->ssid[5],
          params->timeout);
#endif
  rc = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_cnx_params,
                sizeof(struct wifi_connect_req_params));
  if (rc < 0) {
    LOG_ERR("AP mode failed (%d)%s",rc, strerror(-rc));
    return;
  } else {
    LOG_INF("AP mode succeeded %d", rc);
  }
#ifdef CONFIG_NET_DHCPV4_SERVER
  char address_buffer[16];
  LOG_DBG("starting dhcpv4 server with %s", net_addr_ntop(AF_INET, &addr, address_buffer, 16));
  rc = net_dhcpv4_server_start(iface, &addr);
  if(rc < 0) {
    LOG_ERR("Unable to start dhcp server %d", rc);
    return;
  }
#else
  net_ipv4_autoconf_init();
#endif //CONFIG_NET_DHCPV4_SERVER
  mHasAp = true;
  k_sem_give(&wifi_ap_sem);

  LOG_INF("AP mode done");
  return;
}
#endif // CONFIG_ONBOARDING_WIFI_AP

int ob_wifi_connect(void)
{
  int nr_tries = 20;
  int ret = 0;

  struct net_if *iface = net_if_get_wifi_sta();
  LOG_DBG("conntect iface %p", iface);
  if(NULL == iface) {
    LOG_ERR("No interface found");
    return -1;
  }
  LOG_DBG("wifi connect iface %s", iface?iface->config.name:"NULL");

  static struct wifi_connect_req_params cnx_params = {
    .ssid = gSSID,
    .ssid_length = 0,
    .psk = gPSK,
    .psk_length = 0,
    .channel = 0,
    .security = WIFI_SECURITY_TYPE_PSK,
  };
  LOG_DBG("wifi_inited %d", wifi_inited);

#ifdef CONFIG_USE_READY_LED
  ready_led_color(0,255,0);
  ready_led_set(READY_LED_LONG);
#endif

  if(!wifi_inited) {
    LOG_ERR("wifi_init not called\n");
    return -1;
  }


  cnx_params.ssid_length = gSSID_len;
  cnx_params.psk_length = gPSK_len;

  LOG_WRN("WIFI try connecting to %s(%s)...", gSSID, gPSK);
  /* Let's wait few seconds to allow wifi device be on-line */
  while (nr_tries-- > 0) {
    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params,
		   sizeof(struct wifi_connect_req_params));
    if (ret == 0) {
      break;
    }

    LOG_WRN("Connect request failed %d. Waiting for iface to be up...", ret);
    k_msleep(1000);
  }
  if(ret == 0) {
    k_sem_take(&wifi_connect_sem, K_FOREVER);
  }
#ifdef CONFIG_USE_READY_LED
  ready_led_off();
#endif

  LOG_INF("Wifi Connected");
  return ret;
}


