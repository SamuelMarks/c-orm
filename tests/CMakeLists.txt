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

if(CYGWIN)
    file(WRITE ${CMAKE_BINARY_DIR}/fix_paths.cmake "
    if(EXISTS \${TEST_FILE})
        file(READ \${TEST_FILE} content)
        string(REGEX REPLACE \"/cygdrive/([a-zA-Z])/\" \"\\\\1:/\" content \"\${content}\")
        file(WRITE \${TEST_FILE} \"\${content}\")
    endif()
    ")
    add_custom_command(TARGET c-orm-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DTEST_FILE=${CMAKE_BINARY_DIR}/CTestTestfile.cmake -P ${CMAKE_BINARY_DIR}/fix_paths.cmake
        COMMAND ${CMAKE_COMMAND} -DTEST_FILE=${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake -P ${CMAKE_BINARY_DIR}/fix_paths.cmake
    )
endif()
