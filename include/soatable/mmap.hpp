/// @file mmap.hpp
/// @brief Opt-in memory-mapped column storage for very large, trivially-copyable columns.
/// @author Bertin Balouki SIMYELI
///
/// mmap_allocator serves column storage from page-granular virtual memory (mmap on POSIX,
/// VirtualAlloc on Windows) that the OS pages in on demand, so a column can be far larger than the
/// resident set and only the touched pages occupy physical RAM. Mappings are page-aligned, so
/// columns stay SIMD-friendly.
///
/// Composes with the Callocator support as mmap_soa_table. Reserve the column capacity up front
/// (table.reserve) so the backing region is mapped once; growth otherwise remaps and copies.
/// Backing is anonymous demand-paged virtual memory; persistent named-file mapping is a future
/// extension.
#pragma once

#include <cstddef>
#include <limits>
#include <new>

#include "soatable/soatable.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace soatable {

/// @brief A stateless allocator that backs storage with page-granular, demand-paged virtual memory.
/// @tparam T The element type.
template <typename T>
struct mmap_allocator {
    /// @brief The allocated value type.
    using value_type = T;

    /// @brief Rebind helper for container node allocations.
    template <typename U>
    struct rebind {
        using other = mmap_allocator<U>;
    };

    constexpr mmap_allocator() noexcept = default;

    /// @brief Converting constructor required by the Allocator concept.
    template <typename U>
    constexpr mmap_allocator(const mmap_allocator<U>&) noexcept {}

    /// @brief Map storage for @p count elements from demand-paged virtual memory.
    /// @param count The number of elements.
    /// @return Pointer to the page-aligned mapping.
    [[nodiscard]] T* allocate(std::size_t count) {
        if (count == 0) {
            return nullptr;
        }
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        const std::size_t bytes = count * sizeof(T);
#if defined(_WIN32)
        void* ptr = ::VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (ptr == nullptr) {
            throw std::bad_alloc();
        }
#else
        void* ptr =
            ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            throw std::bad_alloc();
        }
#endif
        return static_cast<T*>(ptr);
    }

    /// @brief Release a mapping obtained from allocate().
    /// @param ptr The mapping base address.
    /// @param count The element count passed to allocate().
    void deallocate(T* ptr, std::size_t count) noexcept {
        if (ptr == nullptr) {
            return;
        }
#if defined(_WIN32)
        static_cast<void>(count);
        ::VirtualFree(ptr, 0, MEM_RELEASE);
#else
        ::munmap(ptr, count * sizeof(T));
#endif
    }

    template <typename U>
    constexpr bool operator==(const mmap_allocator<U>&) const noexcept {
        return true;
    }
};

/// @brief A flat table whose columns are backed by demand-paged memory-mapped storage.
/// @tparam Columns The unique, trivially-copyable column types.
template <typename... Columns>
using mmap_soa_table = custom_soa_table<mmap_allocator, Columns...>;

}  // namespace soatable
