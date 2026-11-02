cmake_minimum_required(VERSION 3.28)

project(dank-soulver-core VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json 3.10 REQUIRED)
find_package(CURL REQUIRED)

# Libnumen's API is not stable, so it stays pinned to a release. EXCLUDE_FROM_ALL keeps its headers
# and archive out of our install.
FetchContent_Declare(numen
  GIT_REPOSITORY https://github.com/vicinaehq/numen.git
  GIT_TAG v0.8.1
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(numen)

add_executable(dank-soulver-core
  src/main.cpp
  src/numen_backend.cpp
  src/soulver_backend.cpp
)
target_compile_definitions(dank-soulver-core PRIVATE
  DANK_SOULVER_CORE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(dank-soulver-core PRIVATE
  nlohmann_json::nlohmann_json
  numen::numen
  CURL::libcurl
  ${CMAKE_DL_LIBS}
)
target_compile_options(dank-soulver-core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

install(TARGETS dank-soulver-core RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME helper
    COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_helper.py"
      "$<TARGET_FILE:dank-soulver-core>" "${PROJECT_VERSION}"
  )

  # Runs the plugin's real QML under Quickshell, without a compositor. Skipped if qs is missing.
  add_test(NAME launcher
    COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_launcher.py"
      "$<TARGET_FILE:dank-soulver-core>"
  )
  set_tests_properties(launcher PROPERTIES SKIP_RETURN_CODE 77)
endif()
