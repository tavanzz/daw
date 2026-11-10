/* The mutex and condition variable types Mbed TLS uses (MBEDTLS_THREADING_ALT), backed by
   std::mutex and std::condition_variable_any in net/src/TlsThreading.cpp. */

#ifndef DAW_MBEDTLS_THREADING_ALT_H
#define DAW_MBEDTLS_THREADING_ALT_H

typedef struct mbedtls_platform_mutex_t
{
    void* implementation;
} mbedtls_platform_mutex_t;

typedef struct mbedtls_platform_condition_variable_t
{
    void* implementation;
} mbedtls_platform_condition_variable_t;

#endif
