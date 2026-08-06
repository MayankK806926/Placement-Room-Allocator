cmake_minimum_required(VERSION 3.16)
project(PlacementRoomAllocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build by default for maximum performance
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
add_compile_options(
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
)

# ThreadSanitizer only in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(placement_allocator
    src/main.cpp
    src/allocator/SectionRouter.cpp
)

target_include_directories(placement_allocator PRIVATE src)
target_link_libraries(placement_allocator PRIVATE pthread)

# Copy resource files next to the binary so the default path works
add_custom_command(TARGET placement_allocator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:placement_allocator>/resources
    COMMENT "Copying resource files"
)

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Tests use assert() for correctness checks. The default Release build type
# defines NDEBUG, which compiles every assert() into a no-op — that would make
# every test "pass" without checking anything. Force asserts on for test
# binaries regardless of CMAKE_BUILD_TYPE.
set(TEST_ASSERT_FLAGS -UNDEBUG)

# RingBuffer unit test
add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_include_directories(test_ring_buffer PRIVATE src)
target_link_libraries(test_ring_buffer PRIVATE pthread)
target_compile_options(test_ring_buffer PRIVATE ${TEST_ASSERT_FLAGS})
add_test(NAME RingBufferTest COMMAND test_ring_buffer)

# SectionRouter unit test
add_executable(test_section_router
    tests/test_section_router.cpp
    src/allocator/SectionRouter.cpp
)
target_include_directories(test_section_router PRIVATE src)
target_link_libraries(test_section_router PRIVATE pthread)
target_compile_options(test_section_router PRIVATE ${TEST_ASSERT_FLAGS})
add_test(NAME SectionRouterTest COMMAND test_section_router)

# CompanyQueue unit test
add_executable(test_company_queue
    tests/test_company_queue.cpp
    src/allocator/SectionRouter.cpp
)
target_include_directories(test_company_queue PRIVATE src)
target_link_libraries(test_company_queue PRIVATE pthread)
target_compile_options(test_company_queue PRIVATE ${TEST_ASSERT_FLAGS})
add_test(NAME CompanyQueueTest COMMAND test_company_queue)

# ── Build instructions (printed as a reminder) ───────────────────────────────
message(STATUS "")
message(STATUS "Build:   cmake -B build && cmake --build build")
message(STATUS "Run:     ./build/placement_allocator")
message(STATUS "Tests:   cmake --build build && ctest --test-dir build -V")
message(STATUS "")
