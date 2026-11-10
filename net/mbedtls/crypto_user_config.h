/* Additions to TF-PSA-Crypto's default configuration for MICINSTATION (net/).

   The client and the server use the PSA cryptography subsystem from several threads (a
   thread per connection, the message thread hashing files), so it must be thread safe.
   Mutexes and condition variables come from the C++ standard library
   (net/src/TlsThreading.cpp, threading_alt.h next to this file), the same on every
   platform. */

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
