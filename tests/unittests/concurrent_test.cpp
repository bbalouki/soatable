/// @file concurrent_test.cpp
/// @brief concurrent table: synchronized_table serializes writers against readers.
/// @author Bertin Balouki SIMYELI
///
/// Many reader threads run concurrently with a single ingesting writer; the final state must be
/// consistent and no read may observe a corrupt size.

#include "soatable/concurrent.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "soatable/soatable.hpp"

namespace {
struct Tick {
    double price = 0.0;
};
using Table = soatable::soa_table<Tick>;
}  // namespace

TEST(ConcurrentTest, SingleThreadReadWrite) {
    soatable::synchronized_table<Table> table;
    const auto                          id = table.write([](Table& t) {
        const auto row = t.insert();
        t.assign<Tick>(row, Tick {10.0});
        return row;
    });

    const double price = table.read([&](const Table& t) { return t.get<Tick>(id).price; });
    EXPECT_DOUBLE_EQ(price, 10.0);
    EXPECT_EQ(table.read([](const Table& t) { return t.size(); }), 1U);
}

TEST(ConcurrentTest, ConcurrentReadersWithSingleWriter) {
    soatable::synchronized_table<Table> table;
    constexpr int                       write_count = 2000;
    std::atomic<bool>                   writer_done {false};
    std::atomic<std::size_t>            max_size_seen {0};

    std::vector<std::jthread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            // Concurrently read the size; record the largest value observed for a main-thread
            // check.
            while (!writer_done.load(std::memory_order_relaxed)) {
                const std::size_t size    = table.read([](const Table& t) { return t.size(); });
                std::size_t       current = max_size_seen.load(std::memory_order_relaxed);
                while (size > current && !max_size_seen.compare_exchange_weak(current, size)) {
                }
            }
        });
    }

    {
        std::jthread writer([&] {
            for (int i = 0; i < write_count; ++i) {
                table.write([i](Table& t) {
                    const auto row = t.insert();
                    t.assign<Tick>(row, Tick {static_cast<double>(i)});
                });
            }
            writer_done.store(true, std::memory_order_relaxed);
        });
    }  // Writer joins here; readers then observe writer_done and exit.
    readers.clear();

    EXPECT_EQ(
        table.read([](const Table& t) { return t.size(); }), static_cast<std::size_t>(write_count)
    );
    EXPECT_LE(max_size_seen.load(), static_cast<std::size_t>(write_count));
}
