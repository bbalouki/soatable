/// @file row_handle_test.cpp
/// @brief row_handle convenience wrapper: binding, validity, column access, and unbound-handle
/// errors.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <stdexcept>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;

using DemoHandle = soatable::row_handle<Name, Age, soatable_test::Score>;

TEST(RowHandleTest, DefaultHandleIsInvalid) {
    DemoHandle handle;
    EXPECT_FALSE(handle.is_valid());
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(RowHandleTest, BoundHandleAssignsAndReads) {
    DemoTable  table;
    DemoHandle handle {table.insert(), table};
    ASSERT_TRUE(handle.is_valid());

    handle.assign<Name>("Dora");
    handle.assign<Age>(40);

    EXPECT_EQ(handle.get<Name>().value, "Dora");
    EXPECT_TRUE(handle.contains<Age>());
    EXPECT_NE(handle.try_get<Age>(), nullptr);
}

TEST(RowHandleTest, UnassignAndEraseThroughHandle) {
    DemoTable  table;
    DemoHandle handle {table.insert(), table};
    handle.assign<Age>(1);
    handle.unassign<Age>();
    EXPECT_FALSE(handle.contains<Age>());

    EXPECT_TRUE(handle.erase());
    EXPECT_FALSE(handle.is_valid());
    EXPECT_FALSE(handle.erase());  // Already gone.
}

TEST(RowHandleTest, UnboundHandleThrowsOnAccess) {
    DemoHandle handle;
    EXPECT_THROW(static_cast<void>(handle.get<Age>()), std::logic_error);
}

TEST(RowHandleTest, ConstHandleReadsConstReference) {
    DemoTable  table;
    DemoHandle handle {table.insert(), table};
    handle.assign<Age>(99);

    const DemoHandle& const_handle = handle;
    EXPECT_EQ(const_handle.get<Age>().value, 99);
}
