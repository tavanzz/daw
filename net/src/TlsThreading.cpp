// Mbed TLS's mutexes and condition variables (MBEDTLS_THREADING_ALT), from the C++
// standard library so that every platform has the same ones.

#include <mbedtls/threading.h>

#include <condition_variable>
#include <mutex>

namespace daw::net::detail
{

namespace
{

std::mutex& mutexOf(mbedtls_platform_mutex_t* mutex)
{
    return *static_cast<std::mutex*>(mutex->implementation);
}

std::condition_variable_any& conditionOf(mbedtls_platform_condition_variable_t* condition)
{
    return *static_cast<std::condition_variable_any*>(condition->implementation);
}

int initialiseMutex(mbedtls_platform_mutex_t* mutex)
{
    mutex->implementation = new std::mutex;
    return 0;
}

void destroyMutex(mbedtls_platform_mutex_t* mutex)
{
    delete static_cast<std::mutex*>(mutex->implementation);
    mutex->implementation = nullptr;
}

int lockMutex(mbedtls_platform_mutex_t* mutex)
{
    if (mutex->implementation == nullptr)
        return MBEDTLS_ERR_THREADING_USAGE_ERROR;
    mutexOf(mutex).lock();
    return 0;
}

int unlockMutex(mbedtls_platform_mutex_t* mutex)
{
    if (mutex->implementation == nullptr)
        return MBEDTLS_ERR_THREADING_USAGE_ERROR;
    mutexOf(mutex).unlock();
    return 0;
}

int initialiseCondition(mbedtls_platform_condition_variable_t* condition)
{
    condition->implementation = new std::condition_variable_any;
    return 0;
}

void destroyCondition(mbedtls_platform_condition_variable_t* condition)
{
    delete static_cast<std::condition_variable_any*>(condition->implementation);
    condition->implementation = nullptr;
}

int signalCondition(mbedtls_platform_condition_variable_t* condition)
{
    conditionOf(condition).notify_one();
    return 0;
}

int broadcastCondition(mbedtls_platform_condition_variable_t* condition)
{
    conditionOf(condition).notify_all();
    return 0;
}

int waitForCondition(mbedtls_platform_condition_variable_t* condition,
                     mbedtls_platform_mutex_t* mutex)
{
    conditionOf(condition).wait(mutexOf(mutex));
    return 0;
}

} // namespace

void installTlsThreading()
{
    mbedtls_threading_set_alt(initialiseMutex, destroyMutex, lockMutex, unlockMutex,
                              initialiseCondition, destroyCondition, signalCondition,
                              broadcastCondition, waitForCondition);
}

} // namespace daw::net::detail
