cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

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

if(WIN32 AND BUILD_SHARED_LIBS)
    if(CMAKE_HOST_WIN32)
        set(PATH_SEP ";")
    else()
        set(PATH_SEP ":")
    endif()
    set(CDD_C_PATH_ENV "$ENV{PATH}")

    foreach(tgt curl parson c89stringutils cfs c_cdd c-abstract-http ${C_ORM_SQLITE_TARGET} c-orm c_str_span)
        if(TARGET ${tgt})
            set(CDD_C_PATH_ENV "$<TARGET_FILE_DIR:${tgt}>${PATH_SEP}${CDD_C_PATH_ENV}")
        endif()
    endforeach()

    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        set(CLI_CMD ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:c-orm-cli>)
    else()
        set(CLI_CMD $<TARGET_FILE:c-orm-cli>)
    endif()

    string(REPLACE ";" "\\;" CDD_C_PATH_ENV_ESCAPED "${CDD_C_PATH_ENV}")
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
        COMMAND ${CMAKE_COMMAND} -E env PATH="${CDD_C_PATH_ENV_ESCAPED}" ${CLI_CMD} sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS c-orm-cli "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
        COMMENT "Generating C Models from schema.sql using c-orm-cli..."
    )
else()
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        set(CLI_CMD_STATIC ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:c-orm-cli>)
    else()
        set(CLI_CMD_STATIC $<TARGET_FILE:c-orm-cli>)
    endif()

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
        COMMAND ${CLI_CMD_STATIC} sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS c-orm-cli "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
        COMMENT "Generating C Models from schema.sql using c-orm-cli..."
    )
    endif()

    add_library(e2e_models STATIC "${CMAKE_CURRENT_BINARY_DIR}/Models.c")

    add_executable(c_orm_e2e_tests
    test_arena_uuid.c
    test_ast.c
    test_cache_coverage.c
    test_cli.c
    test_cli_exec.c
    test_db_stubs.c
    test_e2e.c
    test_generic.c
    test_inline_macros.c
    test_memory_driver.c
    test_migrations.c
    test_oom_coverage.c
    test_codegen_coverage.c
    test_query_builder_coverage.c
    test_query_coverage.c
    test_relations.c
    test_sqlite_driver.c
    test_string_builder.c

)

add_dependencies(c_orm_e2e_tests e2e_models)

target_include_directories(c_orm_e2e_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GREATEST_H_DIR}
)

if(MSVC)
    target_compile_definitions(c_orm_e2e_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Pass the c-orm-cli executable path to the tests
# We need to escape backslashes so the C compiler sees it as a valid string literal.
# $<TARGET_FILE:c-orm-cli> usually gives forward slashes in CMake, which is safe for C strings.
target_compile_definitions(c_orm_e2e_tests PRIVATE C_ORM_CLI_EXECUTABLE="$<TARGET_FILE:c-orm-cli>")

  target_link_libraries(c_orm_e2e_tests PRIVATE c-orm c-orm-async c_cdd ${C_ORM_SQLITE_TARGET})

add_test(NAME e2e_test COMMAND c_orm_e2e_tests)
if(WIN32 AND BUILD_SHARED_LIBS AND DEFINED CDD_C_PATH_ENV)
    string(REPLACE ";" "\\;" CDD_C_PATH_ENV_ESCAPED "${CDD_C_PATH_ENV}")
    set_tests_properties(e2e_test PROPERTIES ENVIRONMENT "PATH=${CDD_C_PATH_ENV_ESCAPED}")
endif()

target_include_directories(e2e_models PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(c_orm_e2e_tests PRIVATE e2e_models)
target_link_libraries(e2e_models PRIVATE c-orm)
