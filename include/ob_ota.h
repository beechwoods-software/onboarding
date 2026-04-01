#/*******************************************************************************
 * @file
 * @brief OTA abstraction and public API for the onboarding module
 *
 * This header exposes an abstract interface and result codes used by the
 * onboarding OTA implementations (UpdateHub, Mender, Hawkbit, Golioth).
 * Implementations populate an `ob_ota_vtable_t` and register it with
 * `ota_set_vtable()` so the onboarding code can perform probe/update/reboot
 * operations in a platform-independent way.
 *
 * Copyright(C) 2024  Beechwoods Software, Inc.
 * All Rights Reserved
 */

#pragma once

#ifdef CONFIG_ONBOARDING_OTA_UPDATEHUB
#include "ob_ota_updatehub.h"
#endif // CONFIG_ONBOARDING_OTA_UPDATEHUB
#ifdef CONFIG_ONBORDING_OTA_MENDER
#include "ob_ota_mender.h"
#endif //CONFIG_ONBOARDING_OTA_MENDER
#ifdef CONFIG_ONBOARDING_OTA_HAWKBIT
#include "ob_ota_hawkbit.h"
#endif // CONFIG_ONBOARDING_OTA_HAWKBT
#ifdef CONFIG_ONBOARDING_OTA_GOLIOTH
#include "ob_ota_golioth.h"
#endif // CONFIG_ONBOARDING_OTA_GOLIOTH


#include <stdbool.h>

/**
 * @enum ob_ota_result_t
 * @brief Result codes returned by OTA operations.
 *
 * These codes describe the outcome of probe/update operations and are
 * returned by the vtable functions used by the onboarding module.
 */
typedef enum _ob_ota_result {
  /** Operation completed successfully. */
  OB_OTA_RES_OK = 0,
  /** An update was detected and successfully installed. */
  OB_OTA_RES_UPDATE_INSTALLED,
  /** OTA subsystem has not been initialized. */
  OB_OTA_RES_NOT_INITIALIZED,
  /** A remote update is available. */
  OB_OTA_RES_HAS_UPDATE,
  /** No update is available. */
  OB_OTA_RES_NO_UPDATE,
  /** Network error occurred while checking/downloading update. */
  OB_OTA_RES_NETWORK_ERROR,
  /** Hardware is incompatible with the available update. */
  OB_OTA_RES_INCOMPATIBLE_HARDWARE,
  /** Permission or authentication error accessing update artifact. */
  OB_OTA_RES_PERMISSION_ERROR,
  /** Update image is unconfirmed / pending confirmation. */
  OB_OTA_RES_UNCONFIRMED_IMAGE,
  /** Metadata error parsing update information. */
  OB_OTA_RES_METADATA_ERROR,
  /** Error while downloading the update payload. */
  OB_OTA_RES_DOWNLOAD_ERROR,
  /** Error during installation/activation of the update. */
  OB_OTA_RES_INSTALL_ERROR,
  /** Flash initialization failed. */
  OB_OTA_RES_FLASH_INIT_ERROR,
  /** A previous probe is still in progress. */
  OB_OTA_RES_PROBE_IN_PROGRESS,
  /** The operation was cancelled. */
  OB_OTA_RES_CANCEL,
  /** No response from update server or service. */
  OB_OTA_RES_NO_RESPONSE,
  /** Unknown or unexpected error. */
  OB_OTA_RES_UNKNOWN
} ob_ota_result_t;

/**
 * @brief Called by the OTA implementation to run any periodic/automatic
 * tasks (e.g., to poll for updates). Optional; may be NULL.
 */
typedef void (*ob_ota_autohandler_t)(void);

/**
 * @brief Called to confirm an installed update. Return non-zero on success.
 */
typedef int (*ob_ota_confirm_t)(void);

/**
 * @brief Probe for updates. Return an @ref ob_ota_result_t describing state.
 */
typedef ob_ota_result_t (*ob_ota_probe_t)(void);

/**
 * @brief Download and install the available update. Returns result code.
 */
typedef ob_ota_result_t (*ob_ota_update_t)(void);

/**
 * @brief Request a reboot after successful update installation.
 */
typedef void (*ob_ota_reboot_t)(void);
#ifdef CONFIG_ONBOARDING_DT
/**
 * @brief Callback invoked for device-tree like set operations.
 *
 * @param key   The configuration key to set.
 * @param value The string value to assign.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*ob_dt_callback_t)(const char *key, const char *value);

/**
 * @brief Handler that applies a device-tree style key/value pair and returns
 * an OTA-style result code.
 */
typedef ob_ota_result_t (*ob_dt_set_t)(const char *key, const char *value);
#endif // CONFIG_ONBOARDING_DT
/**
 * @struct _ob_ota_vtable
 * @brief Virtual table for OTA operations
 */
typedef struct _ob_ota_vtable {
  ob_ota_autohandler_t ob_ota_autohandler;
  ob_ota_confirm_t ob_ota_confirm;
  ob_ota_probe_t ob_ota_probe;
  ob_ota_update_t ob_ota_update;
  ob_ota_reboot_t ob_ota_reboot;
#ifdef CONFIG_ONBOARDING_DT
  ob_dt_set_t ob_dt_set;
 #endif // CONFIG_ONBOARDING_DT
} ob_ota_vtable_t;
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the onboard OTA subsystem.
 *
 * This should be called once during platform initialization before other OTA
 * calls. Returns true on success.
 */
bool ota_init(void);

/**
 * @brief Request a reboot via the OTA implementation's reboot handler.
 */
void ota_reboot(void);

/**
 * @brief Trigger download/install of an available update.
 *
 * @return true when update flow was successfully started, false otherwise.
 */
bool ota_do_update(void);

/**
 * @brief Register a platform OTA vtable implementing the OTA operations.
 * @param table Pointer to a populated `ob_ota_vtable_t`.
 */
void ota_set_vtable(ob_ota_vtable_t *table);

/**
 * @brief Set the OTA poll interval (seconds) used by the autohandler.
 * @param interval Poll interval in seconds. A non-positive value disables polling.
 */
void ota_set_poll_interval(int interval);
#ifdef CONFIG_ONBOARDING_DT
/**
 * @brief Set a device-tree-like key/value pair through the OTA device-tree
 * handler (if registered).
 *
 * @param key   Configuration key.
 * @param value Value to set.
 * @return 0 on success, negative errno otherwise.
 */
int ob_dt_set(const char * key, const char *value);

/**
 * @brief Register a callback to handle device-tree set operations.
 * @param callback Function invoked for each set.
 * @return 0 on success, negative errno otherwise.
 */
int ob_dt_set_handler(ob_dt_callback_t callback);
#endif // CONFIG_ONBOARDING_DT

#ifdef __cplusplus
}
#endif
