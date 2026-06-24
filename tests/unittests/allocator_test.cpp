/// @file allocator_test.cpp
/// @brief C.2 allocator-aware columns: a custom allocator is actually used by custom_soa_table, and
/// pmr_soa_table routes column storage through a std::pmr memory resource.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <memory_resource>

#include "soatable/pmr.hpp"
#include "soatable/soatable.hpp"

namespace {
struct Age {
    int value = 0;
};

// A stateless allocator that counts allocations through a shared static counter.
struct alloc_stats {
    static inline std::size_t allocations = 0;
    static void               reset() { allocations = 0; }
};

template <typename T>
struct counting_allocator {
    using value_type = T;

    counting_allocator() = default;
    template <typename U>
    counting_allocator(const counting_allocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t count) {
        ++alloc_stats::allocations;
        return std::allocator<T> {}.allocate(count);
    }
    void deallocate(T* ptr, std::size_t count) noexcept {
        std::allocator<T> {}.deallocate(ptr, count);
    }
    template <typename U>
    bool operator==(const counting_allocator<U>&) const noexcept {
        return true;
    }
};
}  // namespace

TEST(AllocatorTest, CustomAllocatorIsUsedByColumns) {
    alloc_stats::reset();

    soatable::custom_soa_table<counting_allocator, Age> table;
    for (int i = 0; i < 100; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, i);
    }

    EXPECT_GT(alloc_stats::allocations, 0U);
    EXPECT_EQ(table.size(), 100U);
    EXPECT_EQ(table.get<Age>(table.make_row_id(0)).value, 0);
}

TEST(AllocatorTest, PmrTableUsesDefaultMemoryResource) {
    std::array<std::byte, 8192> buffer {};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());
    std::pmr::memory_resource* const    previous = std::pmr::get_default_resource();
    std::pmr::set_default_resource(&resource);

    {
        soatable::pmr_soa_table<Age> table;
        for (int i = 0; i < 50; ++i) {
            const auto id = table.insert();
            table.assign<Age>(id, i * 2);
        }
        EXPECT_EQ(table.size(), 50U);
        EXPECT_EQ(table.get<Age>(table.make_row_id(10)).value, 20);
    }

    std::pmr::set_default_resource(previous);
}

TEST(AllocatorTest, CustomAllocatorTableSupportsFullApi) {
    soatable::custom_soa_table<counting_allocator, Age> table;
    const auto a = table.insert();
    table.assign<Age>(a, 5);
    const auto b = table.insert();
    table.assign<Age>(b, 1);
    table.sort_by_column<Age>([](const Age& l, const Age& r) { return l.value < r.value; });

    auto column = table.column<Age>();
    ASSERT_EQ(column.size(), 2U);
    EXPECT_EQ(column[0].value, 1);
    EXPECT_EQ(column[1].value, 5);
}
