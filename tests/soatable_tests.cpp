#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "soatable/soatable.hpp"

struct Name {
    std::string value;
};
struct Age {
    int value;
};
struct Score {
    float value;
};

using TestTable = soatable::SoaTable<Name, Age, Score>;

TEST(SoaTableTest, BasicInsertAndGet) {
    TestTable table;
    auto      id = table.insert();
    EXPECT_TRUE(table.is_valid(id));

    table.assign<Name>(id, "Alice");
    table.assign<Age>(id, 30);

    EXPECT_EQ(table.get<Name>(id).value, "Alice");
    EXPECT_EQ(table.get<Age>(id).value, 30);
    EXPECT_FALSE(table.contains<Score>(id));
}

TEST(SoaTableTest, EraseAndValidity) {
    TestTable table;
    auto      id = table.insert();
    table.erase(id);
    EXPECT_FALSE(table.is_valid(id));

    auto id2 = table.insert();
    EXPECT_EQ(id.index, id2.index);
    EXPECT_NE(id.generation, id2.generation);
}

TEST(SoaTableTest, SelectRequired) {
    TestTable table;
    auto      id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");

    int count = 0;
    for (auto [id, name, age] : table.select<Name, Age>()) {
        EXPECT_EQ(id, id1);
        EXPECT_EQ(name.get().value, "Alice");
        EXPECT_EQ(age.get().value, 30);
        count++;
    }
    EXPECT_EQ(count, 1);
}

TEST(SoaTableTest, SelectOptional) {
    TestTable table;
    auto      id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");

    int count = 0;
    for (auto [id, name, age_opt] : table.select<Name, std::optional<Age>>()) {
        if (name.get().value == "Alice") {
            EXPECT_TRUE(age_opt.has_value());
            EXPECT_EQ(age_opt->get().value, 30);
        } else {
            EXPECT_EQ(name.get().value, "Bob");
            EXPECT_FALSE(age_opt.has_value());
        }
        count++;
    }
    EXPECT_EQ(count, 2);
}

TEST(SoaTableTest, MultiColumnSort) {
    TestTable table;
    auto      id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");
    table.assign<Age>(id2, 20);

    auto id3 = table.insert();
    table.assign<Name>(id3, "Alice");
    table.assign<Age>(id3, 25);

    // Sort by Name asc, then Age asc
    table.sort_by_multi(
        std::pair<Name, std::function<bool(const Name&, const Name&)>> {
            {}, [](const Name& a, const Name& b) { return a.value < b.value; }
        },
        std::pair<Age, std::function<bool(const Age&, const Age&)>> {
            {}, [](const Age& a, const Age& b) { return a.value < b.value; }
        }
    );

    auto view = table.select<Name, Age>();
    auto it   = view.begin();

    {
        auto [id, name, age] = *it;
        EXPECT_EQ(name.get().value, "Alice");
        EXPECT_EQ(age.get().value, 25);
    }
    ++it;
    {
        auto [id, name, age] = *it;
        EXPECT_EQ(name.get().value, "Alice");
        EXPECT_EQ(age.get().value, 30);
    }
    ++it;
    {
        auto [id, name, age] = *it;
        EXPECT_EQ(name.get().value, "Bob");
        EXPECT_EQ(age.get().value, 20);
    }
}

TEST(SoaTableTest, BatchOps) {
    TestTable        table;
    auto             ids  = table.insert_batch(3);
    std::vector<Age> ages = {Age {10}, Age {20}, Age {30}};
    table.assign_batch<Age>(ids, ages.begin());

    EXPECT_EQ(table.get<Age>(ids[0]).value, 10);
    EXPECT_EQ(table.get<Age>(ids[1]).value, 20);
    EXPECT_EQ(table.get<Age>(ids[2]).value, 30);
}

TEST(UtilityTest, QuantizedFloat) {
    soatable::quantized_float<uint8_t, 0, 1000, 8> q(0.5);
    EXPECT_NEAR(q.get(), 0.5, 0.01);

    q = 1.0;
    EXPECT_NEAR(q.get(), 1.0, 0.01);

    q = 0.0;
    EXPECT_NEAR(q.get(), 0.0, 0.01);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
