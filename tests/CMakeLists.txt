cmake_minimum_required(VERSION 3.10)

# Download greatest.h
include(FetchContent)
file(DOWNLOAD "https://raw.githubusercontent.com/SamuelMarks/greatest/refs/heads/cmake-and-msvc/greatest.h"
     "${CMAKE_CURRENT_BINARY_DIR}/greatest.h")

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(../include)
include_directories(../src)

set(TEST_SOURCES main.c test_c_orm.c fake_db.c)

add_executable(c-orm-test ${TEST_SOURCES})
target_link_libraries(c-orm-test PRIVATE c-orm)

add_test(NAME c-orm-tests COMMAND c-orm-test)
