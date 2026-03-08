cmake_minimum_required(VERSION 3.10)
project(c-orm C)

# Set C standards
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Find Postgres and SQLite3
find_package(PostgreSQL)
find_package(SQLite3)

add_library(c-orm STATIC
    src/c_orm.c
    src/database.c
    src/db_codegen.c
)

target_include_directories(c-orm PUBLIC include)

# Provide placeholders for dependencies that were part of cdd-c
# such as string utils, export macros, etc. Or remove them using scripts like in c-abstract-http.

if(PostgreSQL_FOUND)
    target_link_libraries(c-orm PUBLIC PostgreSQL::PostgreSQL)
endif()

if(SQLite3_FOUND)
    target_link_libraries(c-orm PUBLIC SQLite::SQLite3)
endif()

# Setup Testing using greatest.h just like c-abstract-http
enable_testing()
add_subdirectory(tests)
