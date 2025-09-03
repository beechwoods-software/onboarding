cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_VERBOSE_MAKEFILE 1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")

zephyr_library_named(onboarding)
zephyr_include_directories(include)
zephyr_library_add_dependencies(zephyr_generated_headers)

add_subdirectory_ifdef(CONFIG_LIBST25DV st25dv)
zephyr_library_sources(src/ob_log.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_WIFI src/ob_wifi.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_NVS src/ob_nvs_data.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_CAPTIVE_PORTAL src/ob_captive_portal.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_WEB_SERVER src/ob_web_server.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_BLUETOOTH src/onboarding_bluetooth.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_BLUETOOTH_GATT src/onboarding_bluetooth_gatt.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_BLUETOOTH_BLE src/onboarding_bluetooth_ble.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_NFC src/ob_nfc_tag.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_SHELL src/ob_shell.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_CERTS src/ob_certs.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA src/ob_ota.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA_UPDATEHUB src/ob_ota_updatehub.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA_HAWKBIT src/ob_ota_hawkbit.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA_MENDER src/ob_ota_mender.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA_MENDER src/ob_ota_mender_certs.c)
zephyr_library_sources_ifdef(CONFIG_ONBOARDING_OTA_GOLIOTH src/ob_ota_golioth.c)
if(CONFIG_ONBOARDING_OTA_MENDER MATCHES "y")
  # Amazon Root CA 1 certificate from https://www.amazontrust.com/repository
  # Used as primary CA certificate for Hosted Mender
  generate_inc_file_for_target(app
    "certs/AmazonRootCA1.cer"
    "${ZEPHYR_BINARY_DIR}/include/generated/AmazonRootCA1.cer.inc"
  )

  # CloudFlare CA certificate from Hosted Mender CloudFlare host. Using:
  # curl --write-out '%{certs}' https://c271964d41749feb10da762816c952ee.r2.cloudflarestorage.com
  # Used as secondary CA certificate for Artifacts
  generate_inc_file_for_target(app
    "certs/r2.cloudflarestorage.com1.crt"
    "${ZEPHYR_BINARY_DIR}/include/generated/r2.cloudflarestorage.com1.crt.inc"
  )
endif()
