/*
 * Copyright 2025 Beechwoods Software, Inc Brad Kemp
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
 */

#ifdef CONFIG_ONBOARDING_CERTS
#include <zephyr/kernel.h>

#include <stdint.h>

#include "ob_certs.h"

#ifdef CONFIG_ONBOARDING_CERTS_GENERATE_CERTS
static const uint8_t ca_certificate[] = {
#include "ca_certificate.inc"
};
static const char device_certificate[] = {
#include "device_cert.inc"
};

static const uint8_t device_private_key [] = {
#include "device_privkey.inc"
};
static bool certs_initialized = false;

static void _init_certs(void)
{
  if(!certs_initialized) {
    certs_initialized = true;
  }
}

#endif // CONFIG_ONBOARDING_CERTS_GENERATE_CERTS


const uint8_t * ob_cert_get(ob_cert_type_t type)
{
  const char * cert = NULL;
#ifdef CONFIG_ONBOARDING_CERTS_GENERATE_CERTS
  _init_certs();
  switch( type ) {
  case CA_CERT:
    cert = ca_certificate;
    break;
  case PUBLIC_CERT:
    cert = device_certificate;
    break;
  case  PRIVATE_KEY:
    cert = device_private_key;
    break;
  case SECONDARY_CA_CERT:
    cert = ca_certificate;
    break;
  }
#endif //CONFIG_ONBOARDING_CERTS_GENERATE_CERTS
  return cert;
}

int ob_cert_len(ob_cert_type_t type)
{
  int len = 0;
#ifdef CONFIG_ONBOARDING_CERTS_GENERATE_CERTS
  _init_certs();
  switch( type ) {
  case CA_CERT:
    len = sizeof(ca_certificate);
    break;
  case PUBLIC_CERT:
    len = sizeof(device_certificate);
    break;
  case  PRIVATE_KEY:
    len = sizeof(device_private_key);
    break;
  case SECONDARY_CA_CERT:
    len = sizeof(ca_certificate);
    break;
  }
#endif //CONFIG_ONBOARDING_CERTS_GENERATE_CERTS
  return len;
}
#endif // ONBOARDING_CERTS
  
