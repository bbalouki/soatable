#pragma once

// Shared column types and fixtures used across the SoaTable unit tests. Kept header-only so each
// translation unit compiles them directly; the throwing type's budget is per-process state reset by
// the tests that use it.

#include <stdexcept>
#include <string>

#include "soatable/soatable.hpp"

namespace soatable_test {

// The original demo columns, reused so existing behavioural coverage carries over.
struct Name {
    std::string value;
};
struct Age {
    int value = 0;
};
struct Score {
    float value = 0.0F;
};

using DemoTable = soatable::SoaTable<Name, Age, Score>;

// A move-only column type proves emplace/remove/reorder never rely on copy construction.
struct MoveOnly {
    int value = 0;

    MoveOnly() = default;
    explicit MoveOnly(int initial) : value(initial) {}

    MoveOnly(const MoveOnly&)            = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept        = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    ~MoveOnly()                          = default;
};

// A column type whose constructor throws once a budget of successful constructions is exhausted.
// Copies and moves never throw, so a test can pin exactly which insertion fails and then assert the
// container's strong/basic exception guarantee.
struct ThrowOnNth {
    // Number of further successful constructions allowed; a negative value means unlimited.
    static inline int construct_budget = -1;

    int value = 0;

    ThrowOnNth() : ThrowOnNth(0) {}
    explicit ThrowOnNth(int initial) : value(initial) { consume_budget(); }

    ThrowOnNth(const ThrowOnNth&)            = default;
    ThrowOnNth& operator=(const ThrowOnNth&) = default;
    ThrowOnNth(ThrowOnNth&&) noexcept        = default;
    ThrowOnNth& operator=(ThrowOnNth&&) noexcept = default;
    ~ThrowOnNth()                            = default;

    // Restore the unlimited default so one test cannot leak its budget into another.
    static void reset_budget() noexcept { construct_budget = -1; }

   private:
    static void consume_budget() {
        if (construct_budget == 0) {
            throw std::runtime_error("ThrowOnNth construction budget exhausted.");
        }
        if (construct_budget > 0) {
            --construct_budget;
        }
    }
};

}  // namespace soatable_test
