add_executable(soatable_example soatable_example.cpp)
target_compile_features(soatable_example PRIVATE cxx_std_23)

if(NOT TARGET soatable::soatable)
  find_package(soatable CONFIG REQUIRED)
endif()

target_link_libraries(soatable_example PRIVATE soatable::soatable)

