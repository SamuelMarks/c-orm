cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# Disable tests for dependencies
set(C_CDD_BUILD_TESTING OFF CACHE BOOL "Disable cdd-c tests" FORCE)

# Fetch cdd-c
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../cdd-c")
  FetchContent_Declare(
    cdd_c
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../cdd-c"
  )
else()
  FetchContent_Declare(
    cdd_c
    GIT_REPOSITORY https://github.com/SamuelMarks/cdd-c.git
    GIT_TAG        master
  )
endif()
FetchContent_MakeAvailable(cdd_c)

# Fetch greatest.h
set(GREATEST_H_URL "https://raw.githubusercontent.com/silentbicycle/greatest/master/greatest.h")
set(GREATEST_H_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_downloads")
file(MAKE_DIRECTORY ${GREATEST_H_DIR})
if(NOT EXISTS "${GREATEST_H_DIR}/greatest.h")
    file(DOWNLOAD "${GREATEST_H_URL}" "${GREATEST_H_DIR}/greatest.h" STATUS GREATEST_STATUS)
    list(GET GREATEST_STATUS 0 GREATEST_STATUS_CODE)
    if(NOT GREATEST_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download greatest.h: ${GREATEST_STATUS}")
    endif()
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
    COMMAND cdd-c sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS cdd-c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
    COMMENT "Generating C Models from schema.sql using cdd-c..."
)

add_executable(c_orm_e2e_tests 
    test_e2e.c
    ${CMAKE_CURRENT_BINARY_DIR}/Models.c
)

target_include_directories(c_orm_e2e_tests PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GREATEST_H_DIR}
)

if(MSVC)
    target_compile_definitions(c_orm_e2e_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(c_orm_e2e_tests PRIVATE c-orm)

add_test(NAME e2e_test COMMAND c_orm_e2e_tests)
