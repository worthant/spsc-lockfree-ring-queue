cmake_minimum_required(VERSION 3.16)
project(spsc_ring_queue CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

# Catch2 v3 (no vendored header needed).
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# Move the #if guard in the test file down stage by stage, OR flip this ON.
option(RING_RUN_ALL_TESTS "Compile all staged tests at once" OFF)

# --- tests ---------------------------------------------------------------
add_executable(ring_queue_tests ring_queue_test.cpp)
target_link_libraries(ring_queue_tests PRIVATE Catch2::Catch2WithMain Threads::Threads)
target_compile_options(ring_queue_tests PRIVATE -Wall -Wextra -Wpedantic)
if(RING_RUN_ALL_TESTS)
    target_compile_definitions(ring_queue_tests PRIVATE RING_RUN_ALL_TESTS)
endif()

# --- manual demo ---------------------------------------------------------
add_executable(ring_demo main.cpp)
target_link_libraries(ring_demo PRIVATE Threads::Threads)
target_compile_options(ring_demo PRIVATE -Wall -Wextra -Wpedantic)

# --- ctest registration --------------------------------------------------
enable_testing()
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(ring_queue_tests)

# Run the tests on every build (Exercism-style red/green loop).
add_custom_target(run_tests ALL DEPENDS ring_queue_tests COMMAND ring_queue_tests)
