// Replaces the global operator new and operator delete in the test executable so
// that every heap operation is counted per thread. Only daw_tests links this file;
// the application uses the standard allocator.

#include "support/RealtimeCheck.h"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace
{

// Constant-initialised, so updating it from inside operator new never allocates.
constinit thread_local std::size_t heapOperationCount = 0;

void* allocate(std::size_t size)
{
    ++heapOperationCount;

    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;

    throw std::bad_alloc();
}

void* allocateAligned(std::size_t size, std::align_val_t alignment)
{
    ++heapOperationCount;

    const auto bytes = size == 0 ? std::size_t{1} : size;
    const auto align = static_cast<std::size_t>(alignment);

#if defined(_WIN32)
    if (void* memory = _aligned_malloc(bytes, align))
        return memory;
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, align < sizeof(void*) ? sizeof(void*) : align, bytes) == 0)
        return memory;
#endif

    throw std::bad_alloc();
}

void deallocate(void* memory) noexcept
{
    if (memory == nullptr)
        return;

    ++heapOperationCount;
    std::free(memory);
}

void deallocateAligned(void* memory) noexcept
{
    if (memory == nullptr)
        return;

    ++heapOperationCount;
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

} // namespace

void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(size);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return allocateAligned(size, alignment);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return allocateAligned(size, alignment);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void operator delete(void* memory) noexcept
{
    deallocate(memory);
}

void operator delete[](void* memory) noexcept
{
    deallocate(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    deallocate(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    deallocate(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    deallocate(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    deallocate(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
    deallocateAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    deallocateAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    deallocateAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    deallocateAligned(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocateAligned(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocateAligned(memory);
}

namespace daw::test
{

namespace detail
{

#if defined(DAW_TEST_HAS_RTSAN)
#pragma clang diagnostic push
// Calling an unannotated function from a nonblocking one is the point here.
#pragma clang diagnostic ignored "-Wfunction-effects"

// [[clang::nonblocking]] makes RealtimeSanitizer treat everything called from here
// as audio-thread code. noinline keeps that boundary in optimised builds too.
[[gnu::noinline]] static void callAsRealtime(void (*function)(void*),
                                             void* context) [[clang::nonblocking]]
{
    function(context);
}

#pragma clang diagnostic pop
#endif

std::size_t runAsAudioThread(void (*function)(void*), void* context)
{
    const auto before = heapOperationCount;

#if defined(DAW_TEST_HAS_RTSAN)
    callAsRealtime(function, context);
#else
    function(context);
#endif

    return heapOperationCount - before;
}

} // namespace detail

std::size_t heapOperationsOnThisThread() noexcept
{
    return heapOperationCount;
}

} // namespace daw::test
