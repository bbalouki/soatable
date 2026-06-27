find_package(benchmark CONFIG QUIET)

if(NOT benchmark_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        "v${BENCHMARCK_VERSION}"
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

add_executable(soatable_benchmark soatable_benchmark.cpp)
target_link_libraries(soatable_benchmark PRIVATE soatable::soatable benchmark::benchmark)
