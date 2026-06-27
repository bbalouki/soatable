/// @file serialize_test.cpp
/// @brief serialization: round-trip a table through save()/load(), and reject corrupt or
/// mismatched buffers.
/// @author Bertin Balouki SIMYELI
///
/// Uses trivially-copyable columns, which the fast path requires.

#include "soatable/serialize.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <tuple>
#include <vector>

#include "soatable/soatable.hpp"

namespace {
struct Px {
    double value = 0.0;
};
struct Qty {
    int value = 0;
};
struct Extra {
    long value = 0;
};
using TickTable = soatable::soa_table<Px, Qty>;
}  // namespace

TEST(SerializeTest, RoundTripPreservesValuesAndHandles) {
    TickTable  table;
    const auto a = table.insert();
    table.assign<Px>(a, Px {10.5});
    table.assign<Qty>(a, Qty {3});
    const auto b = table.insert();
    table.assign<Px>(b, Px {20.0});  // No Qty.

    const std::vector<std::byte> bytes = soatable::save(table);

    TickTable restored;
    ASSERT_EQ(soatable::load(restored, bytes), soatable::serialize_status::ok);

    EXPECT_EQ(restored.size(), table.size());
    ASSERT_TRUE(restored.is_valid(a));
    ASSERT_TRUE(restored.is_valid(b));
    EXPECT_DOUBLE_EQ(restored.get<Px>(a).value, 10.5);
    EXPECT_EQ(restored.get<Qty>(a).value, 3);
    EXPECT_DOUBLE_EQ(restored.get<Px>(b).value, 20.0);
    EXPECT_FALSE(restored.contains<Qty>(b));
}

TEST(SerializeTest, RoundTripPreservesGenerationsAfterErase) {
    TickTable  table;
    const auto first = table.insert();
    table.erase(first);  // Bumps the slot's generation.
    const auto second = table.insert();
    table.assign<Px>(second, Px {1.0});

    const std::vector<std::byte> bytes = soatable::save(table);
    TickTable                    restored;
    ASSERT_EQ(soatable::load(restored, bytes), soatable::serialize_status::ok);

    // A stale handle must remain invalid after a round trip; the live one valid.
    EXPECT_FALSE(restored.is_valid(first));
    EXPECT_TRUE(restored.is_valid(second));
}

TEST(SerializeTest, RoundTripPreservesPhysicalOrderAfterSort) {
    TickTable table;
    for (int i = 0; i < 8; ++i) {
        const auto id = table.insert();
        table.assign<Px>(id, Px {static_cast<double>((i * 5) % 7)});
    }
    table.sort_by_column<Px>([](const Px& l, const Px& r) { return l.value < r.value; });

    const std::vector<std::byte> bytes = soatable::save(table);
    TickTable                    restored;
    ASSERT_EQ(soatable::load(restored, bytes), soatable::serialize_status::ok);

    const std::span<const Px> original = table.column<Px>();
    const std::span<const Px> reloaded = restored.column<Px>();
    ASSERT_EQ(original.size(), reloaded.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_DOUBLE_EQ(original[i].value, reloaded[i].value);
    }
}

TEST(SerializeTest, RejectsBadMagic) {
    std::vector<std::byte> garbage(64, std::byte {0x7F});
    TickTable              table;
    EXPECT_EQ(soatable::load(table, garbage), soatable::serialize_status::bad_magic);
}

TEST(SerializeTest, RejectsTruncatedBuffer) {
    TickTable  table;
    const auto id = table.insert();
    table.assign<Px>(id, Px {1.0});

    std::vector<std::byte> bytes = soatable::save(table);
    bytes.resize(bytes.size() / 2);  // Chop the buffer in half.

    TickTable restored;
    EXPECT_EQ(soatable::load(restored, bytes), soatable::serialize_status::truncated);
}

TEST(SerializeTest, RejectsSchemaMismatch) {
    TickTable table;
    std::ignore = table.insert();
    const std::vector<std::byte> bytes = soatable::save(table);

    // A different schema (extra column) must be rejected.
    soatable::soa_table<Px, Qty, Extra> other;
    EXPECT_EQ(soatable::load(other, bytes), soatable::serialize_status::schema_mismatch);
}
