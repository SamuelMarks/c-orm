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
    set(CDD_C_PATH_ENV "$ENV{PATH}")
    foreach(tgt c89stringutils cfs c_cdd c-abstract-http sqlite3 c-orm c_str_span)
        if(TARGET ${tgt})
            set(CDD_C_PATH_ENV "$<TARGET_FILE_DIR:${tgt}>;${CDD_C_PATH_ENV}")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${CDD_C_PATH_ENV}" $<TARGET_FILE:cdd-c> sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS cdd-c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
        COMMENT "Generating C Models from schema.sql using cdd-c..."
    )
else()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Models.h ${CMAKE_CURRENT_BINARY_DIR}/Models.c
        COMMAND $<TARGET_FILE:cdd-c> sql2c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS cdd-c "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
        COMMENT "Generating C Models from schema.sql using cdd-c..."
    )
endif()

add_custom_target(generate_e2e_models
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Models.c ${CMAKE_CURRENT_BINARY_DIR}/Models.h
)

add_executable(c_orm_e2e_tests
    test_e2e.c
    test_inline_macros.c
    test_ast.c
    test_query_coverage.c
    test_migrations.c
    test_relations.c
    test_generic.c
    "${CMAKE_CURRENT_BINARY_DIR}/Models.c"
    )
add_dependencies(c_orm_e2e_tests generate_e2e_models)

target_include_directories(c_orm_e2e_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GREATEST_H_DIR}
)

if(MSVC)
    target_compile_definitions(c_orm_e2e_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(c_orm_e2e_tests PRIVATE c-orm)

add_test(NAME e2e_test COMMAND c_orm_e2e_tests)
if(WIN32 AND BUILD_SHARED_LIBS AND DEFINED CDD_C_PATH_ENV)
    string(REPLACE ";" "\\;" CDD_C_PATH_ENV_ESCAPED "${CDD_C_PATH_ENV}")
    set_tests_properties(e2e_test PROPERTIES ENVIRONMENT "PATH=${CDD_C_PATH_ENV_ESCAPED}")
endif()
