/// @file invariant_test.cpp
/// @brief Property test: drive a deterministic (fixed-seed) sequence of
/// insert/assign/unassign/erase/sort operations and, after every step, assert the table agrees with
/// an independent reference model.
/// @author Bertin Balouki SIMYELI
///
/// This pins the sparse <-> dense <-> data consistency invariant that underpins every operation.

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <random>
#include <vector>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::Name;

namespace {

struct ModelRow {
    soatable::row_id   id {};
    std::optional<int> age;
};

using Table = soatable::soa_table<Age, Name>;

void verify_against_model(const Table& table, const std::vector<ModelRow>& model) {
    ASSERT_EQ(table.size(), model.size());

    std::size_t with_age = 0;
    for (const auto& row : model) {
        ASSERT_TRUE(table.is_valid(row.id));
        ASSERT_EQ(table.contains<Age>(row.id), row.age.has_value());
        if (row.age.has_value()) {
            ASSERT_EQ(table.get<Age>(row.id).value, *row.age);
            ++with_age;
        }
    }

    std::size_t selected = 0;
    for (auto [id, age] : table.select<Age>()) {
        static_cast<void>(id);
        static_cast<void>(age);
        ++selected;
    }
    ASSERT_EQ(selected, with_age);
}

}  // namespace

TEST(InvariantTest, RandomizedOperationSequencePreservesConsistency) {
    Table                 table;
    std::vector<ModelRow> model;
    std::mt19937          rng {12345U};  // Fixed seed: deterministic, reproducible.

    constexpr int iterations = 3000;
    for (int step = 0; step < iterations; ++step) {
        const int op = static_cast<int>(rng() % 100U);

        if (op < 45 || model.empty()) {
            // Insert a row, assigning Age most of the time.
            const auto id = table.insert();
            ModelRow   row {id, std::nullopt};
            if (rng() % 4U != 0U) {
                const int value = static_cast<int>(rng() % 1000U);
                table.assign<Age>(id, value);
                row.age = value;
            }
            model.push_back(row);
        } else if (op < 65) {
            // Erase a random alive row.
            const std::size_t victim = rng() % model.size();
            table.erase(model[victim].id);
            model.erase(model.begin() + static_cast<std::ptrdiff_t>(victim));
        } else if (op < 80) {
            // Reassign Age on a random row.
            const std::size_t target = rng() % model.size();
            const int         value  = static_cast<int>(rng() % 1000U);
            table.assign<Age>(model[target].id, value);
            model[target].age = value;
        } else if (op < 90) {
            // Unassign Age on a random row (may already be absent).
            const std::size_t target = rng() % model.size();
            table.unassign<Age>(model[target].id);
            model[target].age = std::nullopt;
        } else {
            // Physically reorder by Age; handles and values must survive.
            table.sort_by_column<Age>([](const Age& a, const Age& b) { return a.value < b.value; });
        }

        verify_against_model(table, model);
    }
}
