# Onboarding

Onbaording is the process of bringing a device onto a network.
It is usually the first customer expierence with a product.
A great first impression will go along way in forgiving future problems.

## Onboarding process

The onboarding process recieves configuration data from the user and transfers 
it to the device. Below are some of the methods of doing this

* Any device using bluetooth
* Any device using NFC
* Any device using the captive portal

The types of onboarding proccesses that are built depends on the following configuration settigs:
* CONFIG_ONBOARDING_WIFI - This enables the Captive portal
* CONFIG_ONBOARDING_WEB_SERVER - depends on ONBOARDING_WIFI and brings up the device web server
* CONFIG_ONBOARDING_BLUETOOTH - Enables the bluetooth pheripheral
* CONFIG_ONBOARDING_NFC - Enables listening for onboarding credentials via i2c connnected STMicro ST25DV chip
* CONFIG_ONBOARDING_SHELL - shell to manipulate the onboarding processes
* CONFIG_ONBOADING_OTA - Enable Over The Air update support

### Bluetooth Onboarding

When a device boots and does not have wifi credentials configured, it sets its bluetooth name to <CONFIG_XXX_XXX_XX> concatenated with the hex of the 3 last digits of the bluetooth MAC address in ascii format. It then enables bluetooth in one of the modes listed below
* BLE peripheral - advertise as a ble peripheral with  UUID to get configuration information
* GATT - accept GATT connections to pass configuration information

#### BLE Mode

In BLE mode the device advertises the UUID <xxx> a central can use this uudi to read charactaristics which will return the visible SSIDS from the device. The user selects the desired SSID and the central writes charactaristics to set the wifi configuration in the device.

#### GATT mode

In GATT mode the configuring device pairs with the device. It then opens up a GATT connection using UUID <XXXX>. The configuring device and the device uses the following protocol:


### NFC Onboarding

In NFC onboarding mode, the configuring device running the AhoyZephyr app will write onboarding credentials into a ST25DV device connected to the device's i2c bus.  

### Captive Portal Onboarding

When a device boots and does not have wifi credentials configured, it brings up the WIFI in AP mode.
It configures the interface with the value CONFIG_WIFI_AP_ADDRESS and starts a dhcp server to provide addresses to clients. The address pool for the dhcp server is the four IP addresses following the interface address.
A user can then access the devices wifi configuartion web page to configure the device. N.B. It may take a bit of time for this page to load. The web server does a scan of available networks before returning the page.


# Web Server

The web server is a minimalist server. Space takes priority over features. It does not support many of the features that a full feature web server will.

To use the web server call the initialization function init_web_server().
Application web pages can be added to the web server by the register_web_page(const char * pathname, const char * title, ob_web_display_page get_callback,ob_weeb_display_page post_callback, bool home). See ob_web_server.h for documentatin on the paramters.
Calling start_web_server() will start the web server. Calling stop_web_server will stop the web server.

# OTA update
There are five different implmentations of OTA update supported. Golioth, Mender, Updatehub, Hawkbit, Amazon.
The configuration needs the name of the application configured in prj.conf using  CONFIG_ONBOARDING_OTA_NAME, and the version configured using CONFIG_ONBOARDING_OTA_VERSION. Both of these paramters are strings.

## Golioth
Golioth provides and OTA solution as well as a digital twin solution. More information can be found at https://golioth.io/
The Golioth implementation currently only supports PSK authentication. 
The CONFIG_GOLIOTH_PSK_ID and CONFIG_GOLIOTH_PSK are currently in the board specific configuration files. A better solution is needed.

## Hawkbit
Hawkbit is an eclipse project (https://eclipse.dev/hawkbit/). Bosch uses Hawkbit in its IoT Device Management offering.  
The utility provision_hawkbit.py can be found in the zephyrtools repoistory (ssh://git@lm-gitlab.beechwoods.com:7999/zephyr/zephyrtools.git). This utility aids in the provisioning of a device into hawkbit.  To provision a device build the application with HAWKBIT_SHELL enabled. Once running tyep hawkbit info to get the device id. Use proviosion_hawkbit.py to add the device to the hawkbit server.  
provision_hawkbit.py  -r -n Webblinky-e661410403136724 -D "Webblinky device" -b rpi_pico -d e661410403136724

## Mender
Mender (https://mender.io) has ported its Linux/Yocto OTA software to zephyr. It is still in development.

## UpdateHub
Updatehub (https://updatehub.io) provides a tiny OTA implementation. While not as fully featured as its competition, it is the tiniest offering.

## Amazon
The Amazon selection is currently not implmented. 

# NVS
Persistant storage is stored in a flash partition named storage_partition. The nvs can be configured to call back to the application when data is written to the persitant store via the nvs_mirror_callback (see nvs_data.h)
The call back can be used to popluate exernaldata access suvh as digital twin or NFC data.

# Digital Twin

# Logging

Logging is controled by the following entry in the applications Kconfig  
module = ONBOARDING  
module-str = "Onboarding"  
module-help = Enables logging for the onboarding module  
source "subsys/logging/Kconfig.template.log_config  
This will allow an application do enable or disable log levels using the prj.conf file of the application (e.g. CONFIG_ONBOARDING_LOG_LEVEL_DBG=y )  

# Patches

The patches subdirectory containes patches to sources external to this module. The current patches are:  
*zephyr/00_use_APSTA_MODE.patch* - This patch puts an esp32 in APSTA mode instead of AP mode to allow AP mode scans  

# BUGS

The esp32 is not able to connect to an access point after being rebooted when inAP mode.
An OTA should signal to the user while a download is in progress via the ready_led
