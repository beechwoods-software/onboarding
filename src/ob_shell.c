/*
 * Copyright 2023 2024 Beechwoods Software, Inc Brad Kemp
 * All Rights Reserved
 */

#ifdef CONFIG_ONBOARDING_SHELL

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi.h>
#include <zephyr/mgmt/updatehub.h>
#include <zephyr/sys/reboot.h>
#include <version.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

#include "ob_nvs_data.h"

#include "ob_wifi.h"
#include "ob_web_server.h"
#include "ob_ota.h"
LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);

#define OB_HELP_WIFI_NAME "wifi name [name]"
#define OB_HELP_WIFI_SSID "wifi ssid [SSID]"
#define OB_HELP_WIFI_PSK "wifi psk [PSK]"
#define OB_HELP_WIFI_ADDRESS "wifi address <ipv4>"
#define OB_HELP_WIFI_AP_ENABLE "ap enable Enable WiFi AP"
#define OB_HELP_WIFI_AP_DISABLE "ap disable Disable WiFi AP"
#define OB_HELP_WIFI_AP_ADDRESS "ap address [IPv4]"
#define OB_HELP_WEB_START "Start web server"
#define OB_HELP_WEB_STOP  "Stop web server"
#define OB_HELP_WIFI_DHCP_START "Start DHCPv4 client"
#define OB_HELP_WIFI_DHCP_STOP "Stop DHCPv4 client"
#define OB_HELP_FACTORY_RESET "factory reset"
#define OB_HELP_REBOOT "reboot"
#define OB_HELP_OTA_UH_CONFIRM "updatehub confirm"
#define OB_HELP_OTA_UH_PROBE "updatehub probe"
#define OB_HELP_OTA_UH_UPDATE "updatehub update"
#define OB_HELP_OTA_GOLIOTH_PSK "golioth psk [<psk>]"
#define OB_HELP_OTA_GOLIOTH_PSK_ID "golioth psk_id [<psk_id>]"

#ifdef CONFIG_ONBOARDING_WEB_SERVER
/**
 * @brief start the web server
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_web_start(const struct shell *sh, size_t argc,  char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  LOG_INF("web start");
  start_web_server();
  return 0;
}

/**
 * @brief stop the web server
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_web_stop(const struct shell *sh, size_t argc,  char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  LOG_INF("web stop");
  stop_web_server();
  return 0;
}
#endif // CONFIG_ONBOARDING_WEB_SERVER

#ifdef CONFIG_ONBOARDING_WIFI_AP
/**
 * @brief Enable the devices AP
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_ap_enable(const struct shell *sh, size_t argc,  char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  if (argc > 1) {
    LOG_INF("Scheduling ap   argc = %d", (int)argc);
    ob_wifi_ap_enable();
  } else {
    LOG_INF("starting ap   argc = %d", (int)argc);
    ob_wifi_ap_enable();
  }
  return 0;
}

/**
 * @brief Disable the devices AP
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_ap_disable(const struct shell *sh, size_t argc,  char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  ob_wifi_ap_disable();
  return 0;
}

/**
 * @brief Sets the IP address of the devices AP
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ap_address_handler(const struct shell *sh, size_t argc, char ** argv)
{
  if(argc > 1 ) {
    strncpy(wifi_ap_address, argv[1], WIFI_AP_ADDRESS_SIZE);
  }
  LOG_INF("AP Ip address: %s", wifi_ap_address);
  return 0;
}
#endif // CONFIG_ONBOARDING_WIFI_AP

/**
 * @brief Sets the IP address of the devices Wifi
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int setup_iface( const struct shell *sh, size_t argc, char **argv)
{
  struct in_addr addr4;
  struct net_if_addr *ifaddr;

  struct net_if * iface = net_if_get_default();
  if(argc < 2) {
    LOG_ERR("Too few paramters %d", (int)argc);
    return -1;
  }

  if (net_addr_pton(AF_INET, argv[1], &addr4)) {
    LOG_ERR("Invalid address: %s", argv[1]);
    return -EINVAL;
  }

  ifaddr = net_if_ipv4_addr_add(iface, &addr4, NET_ADDR_MANUAL, 0);
  if (!ifaddr) {
    LOG_ERR("Cannot add %s to interface %p", argv[1], iface);
    return -EINVAL;
  }
  return 0;
}
#ifdef CONFIG_ONBOARDING_WIFI_AP
#ifdef CONFIG_NET_DHCPV4_SERVER
/**
 * @brief Starts the dhcp server on the devices AP
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_dhcp_start(const struct shell *sh, size_t argc, char **argv)
{
  //  struct net_if *iface = net_if_get_first_wifi();
  struct net_if *iface = net_if_get_default();
  if(NULL != iface) {
    net_dhcpv4_start(iface);
  } else {
    LOG_ERR("Iface not found");
  }
  return 0;
}
/**
 * @brief Starts the dhcp server on the devices AP
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ob_dhcp_stop(const struct shell *sh, size_t argc, char **argv)
{
  //  struct net_if *iface = net_if_get_first_wifi();
  struct net_if *iface = net_if_get_default();
  if(NULL != iface) {
    net_dhcpv4_stop(iface);
  } else {
    LOG_ERR("Iface not found");
  }
  return 0;
}
#endif // CONFIG_NET_DHCPV4_SERVER
#endif // CONFIG_ONBOARDING_WIFI_AP

#ifdef CONFIG_NET_HOSTNAME_DYNAMIC
/**
 * @brief Sets the host name of the device
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int wifi_name_handler(const struct shell *sh, size_t argc, char**argv)
{
  int rc = 0;
  if(argc > 1) {
    rc = net_hostname_set(argv[1], strlen(argv[1]));
    if(rc < 0) {
      LOG_ERR("set hostname failed %d", rc);
    }
  }
  LOG_INF("hostname %s", net_hostname_get());
  return rc;
}
#endif // CONFIG_NET_HOSTNAME_DYNAMIC

#ifdef CONFIG_ONBOARDING_NVS
/**
 * @brief Reads and writes the SSID to the non volatile store
 *
 * @details if the value of the SSID is absent the current SSID will be printed
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int ssid_handler(const struct shell *sh, size_t argc, char **argv)
{
  int rc = 0;
  char SSID[WIFI_SSID_MAX_LEN+1];
  ob_nvs_data_init();
  if(argc < 2) {
    if((rc = ob_nvs_data_read(NVS_DOMAIN_WIFI, NVS_ID_WIFI_SSID, SSID, WIFI_SSID_MAX_LEN)) < 0) {
      LOG_ERR("Unable to read SSID");
      return -1;
    }
    SSID[rc] = '\0';
    shell_print(sh, "SSID: %s\n", SSID);
  } else {
    if((rc = ob_nvs_data_write(NVS_DOMAIN_WIFI, NVS_ID_WIFI_SSID, argv[1], strlen(argv[1]))) < 0) {
      LOG_ERR("Unable to save SSID %d", rc);
    } else {
      LOG_DBG("Saved SSID %s", argv[1]);
    }
  }
  return rc;
}

/**
 * @brief Reads and writes the PSK to the non volatile store
 *
 * @details if the value of the PSK is absent the current PSK will be printed
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int psk_handler(const struct shell *sh, size_t argc, char **argv)
{
  int rc;
  char PSK[WIFI_PSK_MAX_LEN+1];
  ob_nvs_data_init();
  if(argc < 2) {
    if((rc = ob_nvs_data_read(NVS_DOMAIN_WIFI, NVS_ID_WIFI_PSK, PSK, WIFI_PSK_MAX_LEN)) < 0) {
      LOG_ERR("Unable to read PSK");
      return -1;
    }
    PSK[rc] = '\0';
    shell_print(sh, "PSK: %s\n", PSK);
  }  else {
    if((rc = ob_nvs_data_write(NVS_DOMAIN_WIFI, NVS_ID_WIFI_PSK, argv[1], strlen(argv[1]))) < 0) {
      LOG_ERR("Unable to save PSK %d", rc);
    } else {
      LOG_DBG("Saved PSK %s", argv[1]);
    }
  }
  return rc;
}
#endif // CONFIG_ONBOARDING_NVS

#ifdef CONFIG_ONBOARDING_OTA_UPDATEHUB
static int updatehub_confirm_handler(const struct shell *sh, size_t argc, char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  int rc;
  rc = updatehub_confirm();
  LOG_DBG("Confirm returned %d", rc);
  return rc;
}

static int updatehub_probe_handler(const struct shell *sh, size_t argc, char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  int rc;
  rc = updatehub_probe();
  LOG_DBG("Probe returned %d", rc);
  return rc;
}

static int updatehub_update_handler(const struct shell *sh, size_t argc, char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  int rc;
  rc = updatehub_update();
  LOG_DBG("update returned %d", rc);
  return rc;
}
#endif // CONFIG_ONBOARDING_OTA_UPDATEHUB

#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
static int golioth_psk_handler(const struct shell *sh, size_t argc, char **argv)
{
  int rc;
  char PSK[CONFIG_GOLIOTH_PSK_MAX_LEN+1];
  ob_nvs_data_init();
  if(argc < 2) {
    if((rc = ob_nvs_data_read(NVS_DOMAIN_GOLIOTH,  NVS_ID_OTA_PSK, PSK, CONFIG_GOLIOTH_PSK_MAX_LEN)) < 0) {
      LOG_ERR("Unable to read PSK");
      return -1;
    }
    PSK[rc] = '\0';
    shell_print(sh, "PSK: %s\n", PSK);
  }  else {
    if((rc = ob_nvs_data_write(NVS_DOMAIN_GOLIOTH, NVS_ID_OTA_PSK, argv[1], strlen(argv[1]))) < 0) {
      LOG_ERR("Unable to save PSK %d", rc);
    } else {
      LOG_DBG("Saved PSK %s", argv[1]);
    }
  }
  return rc;
}  
static int golioth_psk_id_handler(const struct shell *sh, size_t argc, char **argv)
{
  int rc;
  char PSK_ID[CONFIG_GOLIOTH_PSK_ID_MAX_LEN+1];
  ob_nvs_data_init();
  if(argc < 2) {
    if((rc = ob_nvs_data_read(NVS_ID_OTA_PSK_ID, PSK_ID, CONFIG_GOLIOTH_PSK_MAX_LEN)) < 0) {
      LOG_ERR("Unable to read PSK_ID");
      return -1;
    }
    PSK_ID[rc] = '\0';
    shell_print(sh, "PSK_ID: %s\n", PSK_ID);
  }  else {
    if((rc = ob_nvs_data_write(NVS_ID_OTA_PSK_ID, argv[1], strlen(argv[1]))) < 0) {
      LOG_ERR("Unable to save PSK %d", rc);
    } else {
      LOG_DBG("Saved PSK %s", argv[1]);
    }
  }
  return rc;
}  
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH

/**
 * @brief Reboots the device
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int cmd_reboot(const struct shell * sh, size_t argc, char ** argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  LOG_WRN("Rebooting");
#ifdef CONFIG_ONBOARDING_OTA
  ota_reboot();
#else // CONFIG_ONBOARDING_OTA
  
#ifdef CONFIG_ONBOARDING_WIFI
  ob_wifi_deinit();
#endif // CONFIG_ONBOARDING_WIFI
  sys_reboot(SYS_REBOOT_COLD);
#endif // CONFIG_ONBOARDING_OTA
  return 0;
}

/**
 * @brief Performs a factor reset on the device.
 *
 * @param sh Pointer to the shell structure.
 * @param argc The number of arguments.
 * @param argv The arguments to the function.
 * @return 0
 */
static int cmd_factory_reset(const struct shell *sh, size_t argc, char **argv)
{
  ARG_UNUSED(sh);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  ob_nvs_data_factory_reset();
  LOG_INF("Factory reset");
  return 0;
}

#ifdef CONFIG_ONBOARDING_WEB_SERVER
/** @brief commands to start and stop the web server */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_web_cmds,
     SHELL_CMD_ARG(start, NULL, OB_HELP_WEB_START, ob_web_start, 0, 0),
     SHELL_CMD_ARG(stop, NULL, OB_HELP_WEB_STOP, ob_web_stop, 0, 0),
     SHELL_SUBCMD_SET_END
     );
#endif // CONFIG_ONBOARDING_WEB_SERVER

#ifdef CONFIG_ONBOARDING_WIFI_AP
/** @brief Commands for handling the devices WIFI AP */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_ap_cmds,
     SHELL_CMD_ARG(enable, NULL, OB_HELP_WIFI_AP_ENABLE, ob_ap_enable, 0, 1),
     SHELL_CMD_ARG(disable, NULL, OB_HELP_WIFI_AP_DISABLE, ob_ap_disable, 0, 0),
     SHELL_CMD_ARG(address, NULL, OB_HELP_WIFI_AP_ADDRESS, ap_address_handler, 1, 1),
     SHELL_SUBCMD_SET_END
     );
#ifdef CONFIG_NET_DHCPV4_SERVER
/** @brief commands for starting and stopping the DHCP server */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_dhcp_cmds,
     SHELL_CMD_ARG(start, NULL, OB_HELP_WIFI_DHCP_START, ob_dhcp_start, 0, 0),
     SHELL_CMD_ARG(stop, NULL, OB_HELP_WIFI_DHCP_STOP, ob_dhcp_stop, 0, 0),
     SHELL_SUBCMD_SET_END
     );
#endif // CONFIG_NET_DHCPV4_SERVER
#endif // CONFIG_ONBOARDING_WIFI_AP

/** @brief commands to handle the device WIFI. */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_wifi_cmds,
#ifdef CONFIG_ONBOARDING_NVS
     SHELL_CMD_ARG(ssid, NULL, OB_HELP_WIFI_SSID, ssid_handler, 1, 1),
     SHELL_CMD_ARG(psk, NULL, OB_HELP_WIFI_PSK, psk_handler, 1, 1),
#endif // CONFIG_ONBOARDING_NVS
                               
#ifdef CONFIG_ONBOARDING_WIFI
     SHELL_CMD_ARG(address, NULL, OB_HELP_WIFI_ADDRESS, setup_iface, 2, 0),
#endif //CONFIG_ONBOARDING_WIFI
                               
#ifdef CONFIG_NET_HOSTNAME_DYNAMIC
     SHELL_CMD_ARG(name, NULL, OB_HELP_WIFI_NAME, wifi_name_handler, 1, 1),
#endif // CONFIG_NET_HOSTNAME_DYNAMIC

     SHELL_SUBCMD_SET_END
     );

#ifdef CONFIG_ONBOARDING_OTA_UPDATEHUB
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_ota_updatehub,
     SHELL_CMD_ARG(confirm, NULL, OB_HELP_OTA_UH_CONFIRM, updatehub_confirm_handler, 0, 0),
     SHELL_CMD_ARG(probe, NULL, OB_HELP_OTA_UH_PROBE, updatehub_probe_handler, 0, 0),
     SHELL_CMD_ARG(update, NULL, OB_HELP_OTA_UH_UPDATE, updatehub_update_handler, 0, 0),
     SHELL_SUBCMD_SET_END
);
#endif // CONFIG_ONBOARDING_OTA_UPDATEHUB

#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob_ota_golioth,
#ifdef CONFIG_GOLIOTH_AUTH_METHOD_PSK
     SHELL_CMD_ARG(psk, NULL, OB_HELP_OTA_GOLIOTH_PSK, golioth_psk_handler, 1, 1),
     SHELL_CMD_ARG(psk_id, NULL, OB_HELP_OTA_GOLIOTH_PSK_ID, golioth_psk_id_handler, 1, 1),
#endif // CONFIG_GOLIOTH_AUTH_METHOD_PSK
     SHELL_SUBCMD_SET_END
);
#endif //CONFIG_ONBOARDING_OTA_GOLIOTH
/** @brief top level commands for onboarding */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ob,
     SHELL_CMD(wifi, &sub_ob_wifi_cmds, "wifi commands", NULL),

#ifdef CONFIG_ONBOARDING_WIFI_AP
     SHELL_CMD(ap, &sub_ob_ap_cmds, "ap copmmands", NULL),
#ifdef CONFIG_NET_DHCPV4_SERVER
     SHELL_CMD(dhcp, &sub_ob_dhcp_cmds, "dhcp commands", NULL),
#endif //CONFIG_NET_DHCPV4_SERVER
#endif // CONFIG_ONBOARDING_WIFI_AP
                               
#ifdef CONFIG_ONBOARDING_WEB_SERVER
     SHELL_CMD(web, &sub_ob_web_cmds, "web commands", NULL),
#endif // CONFIG_ONBOARDING_WEB_SERVER
                               
#ifdef CONFIG_ONBOARDING_OTA_UPDATEHUB
     SHELL_CMD(updatehub, &sub_ob_ota_updatehub, "Updatehub commands", NULL),
#endif // CONFIG_ONBOARDING_OTA__UPDATE_HUB
                               
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
     SHELL_CMD(golioth, &sub_ob_ota_golioth, "Golioth commands", NULL),
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH
                               
     SHELL_CMD_ARG(reboot, NULL, OB_HELP_REBOOT, cmd_reboot, 0, 0),
     SHELL_CMD_ARG(factory_reset, NULL, OB_HELP_FACTORY_RESET, cmd_factory_reset, 0, 0),
     SHELL_SUBCMD_SET_END /* Array terminated. */
     );
/** @brief register the onboarding shell */
SHELL_CMD_REGISTER(ob, &sub_ob, "onboarding commands", NULL);
#endif // CONFIG_ONBOARDING_SHELL
