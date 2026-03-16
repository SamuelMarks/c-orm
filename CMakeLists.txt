cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(c-orm VERSION 0.1.0 LANGUAGES C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Strict warnings
if(MSVC)
    set(C_ORM_STRICT_OPTIONS /W4 /WX /sdl)
else()
    set(C_ORM_STRICT_OPTIONS -Wall -Wextra -Werror -Wpedantic -std=c90)
endif()

add_library(c-orm
    src/c_orm_db.c
    src/c_orm_string_builder.c
    src/c_orm_query_builder.c
    src/c_orm_api.c
    src/c_orm_sqlite.c
    src/c_orm_postgres.c
    src/c_orm_mysql.c
    src/c_orm_oauth2.c
)
set_target_properties(c-orm PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_options(c-orm PRIVATE ${C_ORM_STRICT_OPTIONS})

target_include_directories(c-orm PUBLIC include)

set(C_ORM_SQLITE "LINKED" CACHE STRING "Enable SQLite driver (LINKED, DYNAMIC, OFF)")
set(C_ORM_POSTGRESQL "OFF" CACHE STRING "Enable PostgreSQL driver (LINKED, DYNAMIC, OFF)")
set(C_ORM_MYSQL "OFF" CACHE STRING "Enable MySQL driver (LINKED, DYNAMIC, OFF)")

if(C_ORM_SQLITE STREQUAL "LINKED")
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
        target_link_libraries(c-orm PRIVATE unofficial::sqlite3::sqlite3)
    else()
        find_package(SQLite3 QUIET)
        if(SQLite3_FOUND)
            target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
            target_link_libraries(c-orm PRIVATE SQLite::SQLite3)
        else()
            include(FetchContent)
            FetchContent_Declare(
                sqlite3
                URL https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
            )
            FetchContent_GetProperties(sqlite3)
            if(NOT sqlite3_POPULATED)
                if(POLICY CMP0169)
                    cmake_policy(PUSH)
                    cmake_policy(SET CMP0169 OLD)
                endif()
                FetchContent_Populate(sqlite3)
                if(POLICY CMP0169)
                    cmake_policy(POP)
                endif()
                add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
                target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
            endif()
            target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
            target_link_libraries(c-orm PRIVATE sqlite3)
        endif()
    endif()
elseif(C_ORM_SQLITE STREQUAL "DYNAMIC")
    target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_DYNAMIC C_ORM_ENABLE_SQLITE)
    if(NOT WIN32)
        target_link_libraries(c-orm PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(C_ORM_POSTGRESQL STREQUAL "LINKED")
    find_package(PostgreSQL REQUIRED)
    target_compile_definitions(c-orm PUBLIC C_ORM_POSTGRESQL_LINKED C_ORM_ENABLE_POSTGRESQL)
    target_include_directories(c-orm PRIVATE ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(c-orm PRIVATE PostgreSQL::PostgreSQL)
elseif(C_ORM_POSTGRESQL STREQUAL "DYNAMIC")
    target_compile_definitions(c-orm PUBLIC C_ORM_POSTGRESQL_DYNAMIC)
    if(NOT WIN32)
        target_link_libraries(c-orm PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(C_ORM_MYSQL STREQUAL "LINKED")
    find_package(unofficial-libmariadb CONFIG REQUIRED)
    target_compile_definitions(c-orm PUBLIC C_ORM_MYSQL_LINKED C_ORM_ENABLE_MYSQL)
    target_link_libraries(c-orm PRIVATE unofficial::libmariadb)
elseif(C_ORM_MYSQL STREQUAL "DYNAMIC")
    target_compile_definitions(c-orm PUBLIC C_ORM_MYSQL_DYNAMIC)
    if(NOT WIN32)
        target_link_libraries(c-orm PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

# We will eventually add testing here
enable_testing()
add_subdirectory(tests/e2e)
