if(NOT TARGET soatable::soatable)
  find_package(soatable CONFIG REQUIRED)
endif()

add_executable(soatable_example soatable_example.cpp)
target_compile_features(soatable_example PRIVATE cxx_std_23)
target_link_libraries(soatable_example PRIVATE soatable::soatable)

# Domain cookbooks: one worked example per domain (finance, game/ECS, aerospace, scientific).
set(SOATABLE_COOKBOOKS
  finance_tick_store
  game_ecs
  aerospace_telemetry
  scientific_table
)
foreach(cookbook IN LISTS SOATABLE_COOKBOOKS)
  add_executable(soatable_cookbook_${cookbook} cookbook/${cookbook}.cpp)
  target_compile_features(soatable_cookbook_${cookbook} PRIVATE cxx_std_23)
  target_include_directories(soatable_cookbook_${cookbook} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cookbook)
  target_link_libraries(soatable_cookbook_${cookbook} PRIVATE soatable::soatable)
endforeach()

