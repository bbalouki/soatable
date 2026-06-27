find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        GTest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        "v${GTEST_VERSION}"
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(GTest)
endif()

# Strict warnings for SoaTable's own targets only; GoogleTest headers are not held to -Werror.
function(soatable_apply_strict_warnings target treat_as_errors)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(treat_as_errors)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion)
        if(treat_as_errors)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

#  Unit tests
set(SOATABLE_UNIT_TEST_SOURCES
    unittests/table_test.cpp
    unittests/select_test.cpp
    unittests/sort_test.cpp
    unittests/batch_test.cpp
    unittests/row_handle_test.cpp
    unittests/compression_test.cpp
    unittests/exception_safety_test.cpp
    unittests/invariant_test.cpp
    unittests/view_test.cpp
    unittests/expected_test.cpp
    unittests/span_test.cpp
    unittests/validity_test.cpp
    unittests/serialize_test.cpp
    unittests/aosoa_test.cpp
    unittests/compute_test.cpp
    unittests/query_test.cpp
    unittests/allocator_test.cpp
    unittests/reflect_test.cpp
    unittests/concurrent_test.cpp
    unittests/mmap_test.cpp
    unittests/timeseries_test.cpp
    unittests/units_test.cpp
    unittests/dynamic_test.cpp
    unittests/const_correctness_test.cpp
    unittests/include_hygiene_test.cpp
    unittests/include_hygiene_other_tu.cpp
)

add_executable(soatable_tests ${SOATABLE_UNIT_TEST_SOURCES})
target_link_libraries(soatable_tests PRIVATE soatable::soatable gtest_main)
target_include_directories(soatable_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unittests)
soatable_apply_strict_warnings(soatable_tests FALSE)

include(GoogleTest)
gtest_discover_tests(soatable_tests)

#  Strict-warning compile gate (header only, treated as errors) 
add_executable(soatable_warning_check warning_check.cpp)
target_link_libraries(soatable_warning_check PRIVATE soatable::soatable)
soatable_apply_strict_warnings(soatable_warning_check TRUE)

#  Freestanding / no-exceptions build gate 
add_executable(soatable_no_exceptions_check no_exceptions_check.cpp)
target_link_libraries(soatable_no_exceptions_check PRIVATE soatable::soatable)
target_compile_definitions(soatable_no_exceptions_check PRIVATE SOATABLE_NO_EXCEPTIONS)
if(NOT MSVC)
    target_compile_options(soatable_no_exceptions_check PRIVATE -fno-exceptions)
endif()
add_test(NAME no_exceptions_check COMMAND soatable_no_exceptions_check)

#  Compile-fail tests: each target must fail to build (negative compile-time contracts) 
set(SOATABLE_COMPILE_FAIL_CASES
    duplicate_columns
    unregistered_column
    empty_table
    dimension_mismatch
)

foreach(case IN LISTS SOATABLE_COMPILE_FAIL_CASES)
    add_executable(soatable_compile_fail_${case} EXCLUDE_FROM_ALL compile_fail/${case}.cpp)
    target_link_libraries(soatable_compile_fail_${case} PRIVATE soatable::soatable)
    set_target_properties(soatable_compile_fail_${case} PROPERTIES
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
    )
    add_test(
        NAME compile_fail_${case}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                --target soatable_compile_fail_${case} --config $<CONFIG>
    )
    set_tests_properties(compile_fail_${case} PROPERTIES WILL_FAIL TRUE)
endforeach()
