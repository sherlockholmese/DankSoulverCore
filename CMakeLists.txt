cmake_minimum_required(VERSION 3.20)

project(dank-soulver-core VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json 3.10 REQUIRED)
find_library(SOULVER_WRAPPER_LIBRARY NAMES SoulverWrapper REQUIRED)

add_executable(dank-soulver-core src/main.cpp)
target_link_libraries(dank-soulver-core PRIVATE
  nlohmann_json::nlohmann_json
  "${SOULVER_WRAPPER_LIBRARY}"
)
target_compile_options(dank-soulver-core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

install(TARGETS dank-soulver-core RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_test(NAME cli-version COMMAND dank-soulver-core --version)
  set_tests_properties(cli-version PROPERTIES
    PASS_REGULAR_EXPRESSION "dank-soulver-core ${PROJECT_VERSION}"
  )

  add_test(NAME arithmetic COMMAND dank-soulver-core --json "forty two plus eight")
  set_tests_properties(arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "\\\"value\\\":\\\"50\\\""
  )

  add_test(NAME percentage COMMAND dank-soulver-core --json "15% of 80")
  set_tests_properties(percentage PROPERTIES
    PASS_REGULAR_EXPRESSION "\\\"value\\\":\\\"12\\\""
  )
endif()

