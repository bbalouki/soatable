cmake_minimum_required(VERSION 3.20)

project(SOATABLE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.cache/_deps")
include(${CMAKE_SOURCE_DIR}/cmake/Helpers.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Versions.cmake)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(soatable INTERFACE)
add_library(soatable::soatable ALIAS soatable)

target_compile_features(soatable INTERFACE cxx_std_23)
target_include_directories(soatable INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS soatable
  EXPORT soatableTargets
)

install(EXPORT soatableTargets
  NAMESPACE soatable::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/soatable
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/soatableConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/soatableConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/soatable
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/soatableConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/soatableConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/soatableConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/soatable
)

if(SOATABLE_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/example)
  add_subdirectory(example)
endif()

if(SOATABLE_BUILD_BENCHMARKS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
  add_subdirectory(benchmarks)
endif()

if(SOATABLE_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  enable_testing()
  add_subdirectory(tests)
endif()
