find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        GTest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.20
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(GTest)
endif()

add_executable(soatable_tests soatable_tests.cpp)
target_link_libraries(soatable_tests PRIVATE soatable::soatable gtest_main)

include(GoogleTest)
gtest_discover_tests(soatable_tests)
