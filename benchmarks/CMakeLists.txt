add_executable(soatable_benchmark soatable_benchmark.cpp)
target_compile_features(soatable_benchmark PRIVATE cxx_std_20)

if(NOT TARGET soatable::soatable)
  find_package(soatable CONFIG REQUIRED)
endif()

target_link_libraries(soatable_benchmark PRIVATE soatable::soatable)

