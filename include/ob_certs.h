/*
 * Copyright(C) 2025  Beechwoods Software, Inc.
 * All Rights Reserved
 */
#pragma once

#ifdef CONFIG_ONBOARDING_CERTS


typedef enum ob_cert_type {
  CA_CERT,
  PUBLIC_CERT,
  PRIVATE_KEY,
  SECONDARY_CA_CERT,
  
} ob_cert_type_t;

const uint8_t * ob_cert_get(ob_cert_type_t type);
int ob_cert_len(ob_cert_type_t type);
#endif // CONFIG_ONBOARDING_CERTS
