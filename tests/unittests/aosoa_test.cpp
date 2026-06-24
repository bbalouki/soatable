// A.2 AoSoA tiled storage policy: aosoa_table behaves identically to soa_table for the row API,
// exposes per-tile aligned spans via column_tiles<T>(), and tiles respect the configured size.

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "soatable/soatable.hpp"

namespace {
struct Age {
    int value = 0;
};
struct Name {
    int tag = 0;  // Trivial stand-in so the type stays simple here.
};

constexpr std::size_t tile = 8;
using TiledTable          = soatable::aosoa_table<tile, Age, Name>;
using FlatTable           = soatable::soa_table<Age, Name>;
}  // namespace

TEST(AosoaTest, RowApiMatchesSoa) {
    TiledTable table;
    const auto a = table.insert();
    table.assign<Age>(a, 30);
    table.assign<Name>(a, 1);
    const auto b = table.insert();
    table.assign<Age>(b, 20);

    EXPECT_EQ(table.get<Age>(a).value, 30);
    EXPECT_TRUE(table.contains<Name>(a));
    EXPECT_FALSE(table.contains<Name>(b));

    table.erase(a);
    EXPECT_FALSE(table.is_valid(a));
    EXPECT_EQ(table.get<Age>(b).value, 20);
}

TEST(AosoaTest, SelectAndSortWork) {
    TiledTable table;
    for (int i = 0; i < 20; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, (20 - i) % 13);
        table.assign<Name>(id, i);
    }
    table.sort_by_column<Age>([](const Age& l, const Age& r) { return l.value < r.value; });

    int previous = -1;
    int seen     = 0;
    for (auto [id, age, name] : table.select<Age, Name>()) {
        EXPECT_TRUE(table.is_valid(id));
        EXPECT_GE(age.get().value, previous);
        previous = age.get().value;
        static_cast<void>(name);
        ++seen;
    }
    EXPECT_EQ(seen, 20);
}

TEST(AosoaTest, ColumnTilesCoverAllValuesInOrder) {
    TiledTable table;
    constexpr int row_count = 20;  // More than two tiles of 8.
    for (int i = 0; i < row_count; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, i);
    }

    std::vector<int> flattened;
    for (const std::span<Age> tile_span : table.column_tiles<Age>()) {
        EXPECT_LE(tile_span.size(), tile);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(tile_span.data()) % soatable::simd_alignment, 0U);
        for (const Age& age : tile_span) {
            flattened.push_back(age.value);
        }
    }

    ASSERT_EQ(flattened.size(), static_cast<std::size_t>(row_count));
    for (int i = 0; i < row_count; ++i) {
        EXPECT_EQ(flattened[static_cast<std::size_t>(i)], i);
    }
}

TEST(AosoaTest, FullTilesRespectTileSize) {
    TiledTable table;
    for (int i = 0; i < 17; ++i) {  // 8 + 8 + 1
        const auto id = table.insert();
        table.assign<Age>(id, i);
    }

    const auto tiles = table.column_tiles<Age>();
    ASSERT_EQ(tiles.size(), 3U);
    EXPECT_EQ(tiles[0].size(), tile);
    EXPECT_EQ(tiles[1].size(), tile);
    EXPECT_EQ(tiles[2].size(), 1U);
}

TEST(AosoaTest, MutationThroughTileIsVisible) {
    TiledTable table;
    const auto id = table.insert();
    table.assign<Age>(id, 5);

    auto tiles = table.column_tiles<Age>();
    ASSERT_FALSE(tiles.empty());
    tiles[0][0].value = 99;

    EXPECT_EQ(table.get<Age>(id).value, 99);
}

TEST(AosoaTest, ProducesSameSelectionAsSoaUnderSameOps) {
    TiledTable tiled;
    FlatTable  flat;
    for (int i = 0; i < 50; ++i) {
        const auto t = tiled.insert();
        const auto f = flat.insert();
        if (i % 3 != 0) {
            tiled.assign<Age>(t, i);
            flat.assign<Age>(f, i);
        }
    }

    std::vector<int> from_tiled;
    for (auto [id, age] : tiled.select<Age>()) {
        static_cast<void>(id);
        from_tiled.push_back(age.get().value);
    }
    std::vector<int> from_flat;
    for (auto [id, age] : flat.select<Age>()) {
        static_cast<void>(id);
        from_flat.push_back(age.get().value);
    }
    EXPECT_EQ(from_tiled, from_flat);
}

TEST(AosoaTest, SoaColumnTilesReturnsSingleWholeColumn) {
    FlatTable table;
    for (int i = 0; i < 10; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, i);
    }
    const auto tiles = table.column_tiles<Age>();
    ASSERT_EQ(tiles.size(), 1U);
    EXPECT_EQ(tiles[0].size(), 10U);
}
