cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Fetch greatest.h
include(FetchContent)
FetchContent_Declare(
  greatest
  GIT_REPOSITORY https://github.com/SamuelMarks/greatest.git
  GIT_TAG master
)
FetchContent_MakeAvailable(greatest)
set(GREATEST_H_DIR "${greatest_SOURCE_DIR}")

if(WIN32 AND BUILD_SHARED_LIBS)
    if(CMAKE_HOST_WIN32)
        set(PATH_SEP ";")
    else()
        set(PATH_SEP ":")
    endif()
    set(CDD_C_PATH_ENV "$ENV{PATH}")

    foreach(tgt curl CURL::libcurl parson c89stringutils cfs c_cdd c-abstract-http ${C_ORM_SQLITE_TARGET} c-orm c_str_span)
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
    set(RUN_ENV_VARS PATH="${CDD_C_PATH_ENV_ESCAPED}")
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR MATCHES "wine")
    endif()

    if (CMAKE_CROSSCOMPILING_EMULATOR MATCHES "wine")
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.c" "${CMAKE_CURRENT_BINARY_DIR}/Models.c"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.h" "${CMAKE_CURRENT_BINARY_DIR}/Models.h"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.c" "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.h"
            COMMENT "Using pre-generated C Models for Wine MSVC Shared build..."
        )
    else()
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
            COMMAND ${CMAKE_COMMAND} -E env ${RUN_ENV_VARS} ${CLI_CMD} sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS c-orm-cli "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
            COMMENT "Generating C Models from schema.sql using c-orm-cli..."
        )
    endif()
else()
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
        set(CLI_CMD_STATIC ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:c-orm-cli>)
    else()
        set(CLI_CMD_STATIC $<TARGET_FILE:c-orm-cli>)
    endif()

    set(RUN_ENV_VARS "")
    if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR MATCHES "wine")
    endif()

    if (CMAKE_CROSSCOMPILING_EMULATOR MATCHES "wine")
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.c" "${CMAKE_CURRENT_BINARY_DIR}/Models.c"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.h" "${CMAKE_CURRENT_BINARY_DIR}/Models.h"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.c" "${CMAKE_CURRENT_SOURCE_DIR}/pregen/Models.h"
            COMMENT "Using pre-generated C Models for Wine MSVC build..."
        )
    else()
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
            COMMAND ${CMAKE_COMMAND} -E env ${RUN_ENV_VARS} ${CLI_CMD_STATIC} sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS c-orm-cli "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
            COMMENT "Generating C Models from schema.sql using c-orm-cli..."
        )
    endif()
    endif()

    add_library(e2e_models STATIC "${CMAKE_CURRENT_BINARY_DIR}/Models.c")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Models.c" PROPERTIES COMPILE_DEFINITIONS "")

    add_executable(c_orm_e2e_tests
    test_arena_uuid.c
    test_ast.c
    test_cache_coverage.c
    test_cli.c
    test_cli_exec.c
    test_db_stubs.c
    test_e2e.c
    test_api_coverage.c
    test_generic.c
    test_abstract_struct.c
    test_cdd_c_ir.c
    test_query_projection.c
    test_c_orm_sql.c
    test_c_orm_sql_extra.c
    test_hydrate_router.c
    test_migration.c
    test_c_orm_c_to_sql.c
    test_c_orm_sql_to_c.c
    test_inline_macros.c
    test_memory_driver.c
    test_migrations.c
    test_oom_coverage.c
    test_codegen_coverage.c
    test_query_builder_coverage.c
      test_orm_gen.c
    test_query_coverage.c
    test_relations.c
    test_sqlite_driver.c
    test_string_builder.c
    test_sql_parser.c
    test_oauth2.c
    test_models_coverage.c
)

add_dependencies(c_orm_e2e_tests e2e_models)

target_include_directories(c_orm_e2e_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GREATEST_H_DIR}
)

if(C_ORM_STRICT_OPTIONS)
    target_compile_options(c_orm_e2e_tests PRIVATE ${C_ORM_STRICT_OPTIONS})
    target_compile_options(e2e_models PRIVATE ${C_ORM_STRICT_OPTIONS})
endif()

if(MSVC)
endif()

if(EMSCRIPTEN)
    target_link_options(c_orm_e2e_tests PRIVATE -lidbfs.js "SHELL:-s EXIT_RUNTIME=1" "SHELL:-s USE_PTHREADS=1" "SHELL:-s STACK_SIZE=5MB" "SHELL:-s EMULATE_FUNCTION_POINTER_CASTS=1")
endif()

# Pass the c-orm-cli executable path to the tests
# We need to escape backslashes so the C compiler sees it as a valid string literal.
# $<TARGET_FILE:c-orm-cli> usually gives forward slashes in CMake, which is safe for C strings.
target_compile_definitions(c_orm_e2e_tests PRIVATE C_ORM_CLI_EXECUTABLE="$<TARGET_FILE:c-orm-cli>")

  target_link_libraries(c_orm_e2e_tests PRIVATE c-orm c-orm-async c_cdd ${C_ORM_SQLITE_TARGET})

add_test(NAME e2e_test COMMAND c_orm_e2e_tests)
if(WIN32 AND BUILD_SHARED_LIBS AND DEFINED CDD_C_PATH_ENV AND NOT CMAKE_CROSSCOMPILING)
    string(REPLACE ";" "\\;" CDD_C_PATH_ENV_ESCAPED "${CDD_C_PATH_ENV}")
    set_tests_properties(e2e_test PROPERTIES ENVIRONMENT "PATH=${CDD_C_PATH_ENV_ESCAPED}")
endif()

target_include_directories(e2e_models PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(c_orm_e2e_tests PRIVATE e2e_models)
target_link_libraries(e2e_models PRIVATE c-orm)
