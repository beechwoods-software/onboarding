
#include <zephyr/sys/reboot.h>

#include "ob_ota.h"
#include "ob_web_server.h"
#include "ob_wifi.h"


void ob_reboot(void)
{

#ifdef CONFIG_ONBOARDING_OTA
  ota_reboot();
#else // CONFIG_ONBOARDING_OTA
#ifdef CONFIG_ONBOARDING_WEB_SERVER
  stop_web_server();
#endif
#ifdef CONFIG_ONBOARDING_WIFI
  ob_wifi_deinit();
#endif // CONFIG_ONBOARDING_WIFI
  sys_reboot(SYS_REBOOT_COLD);
#endif // CONFIG_ONBOARDING_OTA

}
