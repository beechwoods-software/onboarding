/*
 * Copyright 2024  Brad Kemp Beechwoods Software, Inc
 * All Rights Reserved
 */

#include <zephyr/kernel.h>
#include <zephyr/net/wifi.h>
#include <zephyr/sys/reboot.h>
#include <stdlib.h>
#include "ob_web_server.h"
#include "ob_wifi.h"
#include "ob_nvs_data.h"

#ifdef CONFIG_ONBOARDING_CAPTIVE_PORTAL
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

/**
 * @brief The path of the web page.
 */
#define WIFI_SETUP_PAGE_PATH "/setwifi.html"
/**
 * @brief The title of the web page.
 */
#define WIFI_SETUP_TITLE "Wifi setup"


/**
 * @brief This is the start of the body of the WIFI_SETUP_PAGE_PATH.
 */
static char content_wifi_body_start[] = {
  "<form method=\"post\" enctype=\"text/plain\" action=\"" WIFI_SETUP_PAGE_PATH "\"><div><label for=\"ssid\">Select a SSID:</label><select name=\"ssid\" id=\"ssid\"> "};

/**
 * @brief this is the buffer that contains the SSIDs and the PSK for discovered APs.
 *  It is filled in by the client_scan_done callback
 * @see client_scan_done
 */
static char *content_wifi_body_ssid = NULL;

/**
 * @brief This tail of the body of the WIFI_SETUP_PAGE_PATH
 */
static char content_wifi_body_tail[] = {
  "</select></div><div><label for=\"pass\">Password (8 characters minimum):</label><input type=\"password\" id=\"pass\" name=\"password\" minlength=\"8\" required /></div>"
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
  "<div><label for=\"pskid\">Golioth PSK_ID:</label><input type=\"text\" id=\"pskid\" name=\"pskid\"  /></div>"
  "<div><label for=\"psk\">Golioth PSK:</label><input type=\"password\" id=\"psk\" name=\"psk\" /></div>"
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH
  "<input type=\"submit\" value=\"Configure\" /></form></body></html>\r\n\r\n"};

/**
 * @brief The format for displaying SSIDs
 */
char option_fmt[] = "<option value=\"%s\">%s</option>";

/**
 * @brief the number of attributes in the POST
 */
#define NUM_WIFI_SETUP_ATTRIBUTES 2

#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
#undef NUM_WIFI_SETUP_ATTRIBUTES
#define NUM_WIFI_SETUP_ATTRIBUTES 4
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH

/**
 * @brief the index of the SSID post attribute
 */
#define WIFI_SETUP_ATTRIB_SSID      0

/**
 * @brief the index of the pre shared key post attribute
 */
#define WIFI_SETUP_ATTRIB_PASSWORD  1

#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
#define WIFI_SETUP_ATTRIB_OTA_PSK   2
#define WIFI_SETUP_ATTRIB_OTA_PSKID 3
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH

/**
 * @var wifi_setup_attrib
 * @brief This variable holds the attributes that are returned from a
 * POST to the  WIFI_SETUP_PAGE_PATH web page
 */
post_attributes_t wifi_setup_attrib[NUM_WIFI_SETUP_ATTRIBUTES] = {
  /**
   * @brief the SSID of the selected WIFI AP
   */
  { "ssid", 4, },
  /**
   * @brief the pre shared key of the WIFI AP
   */
  { "password", 8},
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
  { "psk", 64},
  { "pskid", 64},
#endif //CONFIG_ONBOARDING_OTA_GOLIOTH
};
/**
 * @brief a semaphore to pend opertation until the wifi scan is complete
 *
 */
static K_SEM_DEFINE(scan_done_sem, 0, 1);

/**
 * @brief This function is called when a wifi scan completes. @n
 * This function iterates through the passed list of ssids
 * to find the size of the buffer needed to hold all the ssids. @n
 * It then allocates and constructs the buffer for selecting an SSID.
 *
 * @param ssid Pointer to the linked list of ssids detected
 */
static void client_scan_done(ssid_item_t * ssid)
{
  ssid_item_t * it;
  char ssidbuf[(WIFI_SSID_MAX_LEN * 2) + sizeof(option_fmt)];
  int len = 0;
  LOG_DBG("Wifi scan done");
  for(it = ssid; NULL != it; it=it->next) {
    //    len += it->len * 2 ;
    len += strlen(it->ssid) * 2 ;
    len += (sizeof(option_fmt) -4);
  }

  content_wifi_body_ssid = malloc(len);
  //content_wifi_body_ssid = ssid_data;
  if(NULL == content_wifi_body_ssid) {
    LOG_ERR("scan done no memory for %d", len);
    return;
  }
  LOG_DBG("Allocated %d bytes", len);
  memset(content_wifi_body_ssid, 0, len);

  int altlen = 0;
  for(it = ssid; NULL != it; it=it->next) {
    sprintf(ssidbuf, option_fmt, it->ssid, it->ssid);
    LOG_DBG("ssid buf %s %d", ssidbuf, (int)strlen(ssidbuf));
    altlen += strlen(ssidbuf);
    strcat(content_wifi_body_ssid, ssidbuf);
  }
  LOG_DBG("buf %s altlen %d", content_wifi_body_ssid, altlen);
  k_sem_give(&scan_done_sem);

}

/**
 * @brief This function sends the GET page for the captive portal.
 *
 * @param client The socket to send the page over.
 * @param wp The web_page_t structure for this page
 *
 * @return 0 on success
 * @return -1 on failure
 */
static int display_wifi_setup_page(int client, web_page_t * wp)
{
  int contentlen;
  char * header = NULL;
  int rc = 0;

  LOG_DBG("Wifi Setup");
  set_scan_done_callback(client_scan_done);
  ob_wifi_scan();
  k_sem_take(&scan_done_sem, K_FOREVER);
  // TODO add a queue to pass ssid string
  do {
    if(NULL == content_wifi_body_ssid) {
      rc = -1;
      break;
    }
    contentlen = strlen(content_wifi_body_start) + strlen(content_wifi_body_ssid) + strlen(content_wifi_body_tail);
    header = CreateHeader200(contentlen, "Wifi setup");
    if(NULL == header) {
      LOG_ERR("HTTP header creation failed");
      rc = -1;
      break;
    }

    rc = sendall(client, header, strlen(header));
    if(rc < 0) {
      LOG_ERR("HTTP Header send failed %d",errno);
    }
    rc = sendall(client, content_wifi_body_start, strlen(content_wifi_body_start));
    if(rc < 0) {
      LOG_ERR("HTTP wifi_body_start send failed %d",errno);
    }
    rc = sendall(client, content_wifi_body_ssid, strlen(content_wifi_body_ssid));
    if(rc < 0) {
      LOG_ERR("HTTP wifi_body_ssid send failed %d",errno);
    }
    rc = sendall(client, content_wifi_body_tail, strlen(content_wifi_body_tail));
    if(rc < 0) {
      LOG_ERR("HTTP wifi_body_tail send failed %d",errno);

    }
  } while(0);
  if(NULL != content_wifi_body_ssid) {
    free(content_wifi_body_ssid);
  }
  return rc;
}

/**
 * @brief This function processes a post to the WIFI_SETUP_PAGE_PATH web page. @n
 * The function processes the POST from a client. @n It extracts the SSID and the PSK
 * and saves them into nvs storage. @n
 * It sends the web server home page back to the client and reboots the device
 *
 * @return 0 on success
 * @return -1 on error
 */
static int post_wifi_setup_page(int client, web_page_t * wp)
{
  int rc;
  if((rc =  ob_ws_process_post(client, wifi_setup_attrib, NUM_WIFI_SETUP_ATTRIBUTES,wp)) >= 0) {
    if((rc = ob_nvs_data_write(NVS_DOMAIN_WIFI, NVS_ID_WIFI_SSID,
                            wifi_setup_attrib[WIFI_SETUP_ATTRIB_SSID].valuebuffer,
                            strlen(wifi_setup_attrib[WIFI_SETUP_ATTRIB_SSID].valuebuffer))) < 0) {
      LOG_ERR("Unable to save SSID %d", rc);
    }
    if((rc = ob_nvs_data_write(NVS_DOMAIN_WIFI, NVS_ID_WIFI_PSK,
                            wifi_setup_attrib[WIFI_SETUP_ATTRIB_PASSWORD].valuebuffer,
                            strlen(wifi_setup_attrib[WIFI_SETUP_ATTRIB_PASSWORD].valuebuffer))) < 0) {
      LOG_ERR("Unable to save PSK %d", rc);
    }
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
    if((rc = ob_nvs_data_write(NVS_DOMAIN_OTA, NVS_ID_OTA_PSK,
                            wifi_setup_attrib[WIFI_SETUP_ATTRIB_OTA_PSK].valuebuffer,
                            strlen(wifi_setup_attrib[WIFI_SETUPATTRIB_OTA_PSK].valuebuffer))) < 0) {
      LOG_ERR("Unable to save Golioth PSK %d", rc);
    }
    if((rc = ob_nvs_data_write(NVS_DOMAIN_OTA, NVS_ID_OTA_PSK_ID,
                            wifi_setup_attrib[WIFI_SETUP_ATTRIB_OTA_PSKID].valuebuffer,
                            strlen(wifi_setup_attrib[WIFI_SETUP_ATTRIB_OTA_PSKID].valuebuffer))) < 0) {
      LOG_ERR("Unable to save Golioth PSK_ID %d", rc);
    }
    
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH
  } else {
    LOG_ERR("Post proccess failed %d", rc);
  }
  ob_web_server_display_home(client);

  if(rc >= 0) {
    ob_wifi_deinit();
    sys_reboot(SYS_REBOOT_COLD);
  }
  return rc;
}

/**
 * @brief This function registers the captive portal web page with the ob_web_server
 * @see ob_web_server
 */
int
ob_cp_init()
{
  int rc = 0;
  rc = ob_ws_register_web_page(WIFI_SETUP_PAGE_PATH,
                               WIFI_SETUP_TITLE,
                               display_wifi_setup_page,
                               post_wifi_setup_page,
                               PAGE_IS_CAPTIVE_PORTAL);
  return rc;

}

#endif // CONFIG_ONBOARDING_CAPTIVE_PORTAL
