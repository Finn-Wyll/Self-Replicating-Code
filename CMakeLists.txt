cmake_minimum_required(VERSION 3.20)
project(adaptogen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ADAPTOGEN_BUILD_TESTS "Build unit tests" ON)
option(ADAPTOGEN_BUILD_BENCH "Build the algorithm comparison tool" ON)

include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

find_package(Threads REQUIRED)

add_library(adaptogen
  src/instruction.cpp
  src/vm.cpp
  src/problem.cpp
  src/analysis.cpp
  src/variation.cpp
  src/engine.cpp
  src/serialize.cpp
  src/registry.cpp
  src/strain.cpp
  src/problems/formula.cpp
  src/problems/lists.cpp)
target_include_directories(adaptogen PUBLIC include)
target_link_libraries(adaptogen PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(adaptogen PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

add_executable(adaptogen-cli src/cli/main.cpp)
set_target_properties(adaptogen-cli PROPERTIES OUTPUT_NAME adaptogen)
target_link_libraries(adaptogen-cli PRIVATE adaptogen)

if(ADAPTOGEN_BUILD_BENCH)
  add_executable(adaptogen-compare bench/compare.cpp bench/baselines.cpp)
  target_link_libraries(adaptogen-compare PRIVATE adaptogen)
endif()

if(ADAPTOGEN_BUILD_TESTS)
  enable_testing()
  add_executable(adaptogen-tests tests/tests.cpp)
  target_link_libraries(adaptogen-tests PRIVATE adaptogen)
  add_test(NAME adaptogen-tests COMMAND adaptogen-tests)
endif()
