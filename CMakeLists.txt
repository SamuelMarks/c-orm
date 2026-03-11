cmake_minimum_required(VERSION 3.15)
project(c-orm C)

if(CYGWIN)
    execute_process(COMMAND cygpath -m ${CMAKE_MAKE_PROGRAM}
                    OUTPUT_VARIABLE WIN_MAKE_PROGRAM
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_MAKE_PROGRAM ${WIN_MAKE_PROGRAM} CACHE FILEPATH "Make program" FORCE)
endif()

# Set C standards
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(C_ORM_BACKEND_SQLITE "Enable SQLite backend" ON)
option(C_ORM_BACKEND_POSTGRES "Enable PostgreSQL backend" OFF)
option(C_ORM_BACKEND_MYSQL "Enable MySQL backend" OFF)

# Shared CI toggles
set(CDD_CHARSET "UTF8" CACHE STRING "Charset to use (UNICODE, ANSI, UTF8)")
set(CDD_THREADING "ON" CACHE STRING "Multi-threaded (ON/OFF)")
set(CDD_MSVC_RTC "OFF" CACHE STRING "MSVC Runtime Checks (RTC1, RTCs, RTCu, OFF)")
set(CDD_CRT_LINKAGE "Shared" CACHE STRING "CRT Linkage (Static, Shared)")
if(MSVC)
    if(CDD_CRT_LINKAGE STREQUAL "Static")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()


if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION AND C_ORM_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if(NOT ipo_supported)
        message(WARNING "IPO is not supported on this platform")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
    endif()
endif()

if(NOT C_ORM_BACKEND_SQLITE AND NOT C_ORM_BACKEND_POSTGRES AND NOT C_ORM_BACKEND_MYSQL)
    message(FATAL_ERROR "At least one backend must be selected (C_ORM_BACKEND_SQLITE, C_ORM_BACKEND_POSTGRES, or C_ORM_BACKEND_MYSQL)")
endif()

# Find Backend Dependencies
if(C_ORM_BACKEND_SQLITE)
    find_package(SQLite3)
    if(NOT SQLite3_FOUND)
        message(WARNING "SQLite3 not found, but C_ORM_BACKEND_SQLITE is ON.")
    endif()
endif()

if(C_ORM_BACKEND_POSTGRES)
    find_package(PostgreSQL)
endif()

include(FetchContent)

# Depend on cdd-c for parsing & emitting C code
FetchContent_Declare(
  cdd_c
  GIT_REPOSITORY https://github.com/SamuelMarks/cdd-c.git
  GIT_TAG        master
)
if(NOT cdd_c_POPULATED)
    FetchContent_Populate(cdd_c)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(c-orm
    src/c_orm.c
    src/database.c
    src/db_codegen.c
)

target_include_directories(c-orm PUBLIC include)
set_target_properties(c-orm PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(MSVC)
    if(CDD_MSVC_RTC STREQUAL "RTC1")
        target_compile_options(c-orm PRIVATE /RTC1)
    elseif(CDD_MSVC_RTC STREQUAL "RTCs")
        target_compile_options(c-orm PRIVATE /RTCs)
    elseif(CDD_MSVC_RTC STREQUAL "RTCu")
        target_compile_options(c-orm PRIVATE /RTCu)
    endif()
endif()

if(CDD_CHARSET STREQUAL "UNICODE")
    target_compile_definitions(c-orm PUBLIC UNICODE _UNICODE)
elseif(CDD_CHARSET STREQUAL "ANSI")
    target_compile_definitions(c-orm PUBLIC _MBCS)
endif()

if(CDD_THREADING STREQUAL "OFF")
    target_compile_definitions(c-orm PUBLIC C_ORM_SINGLE_THREADED)
endif()
if(C_ORM_BACKEND_SQLITE)
    target_compile_definitions(c-orm PUBLIC C_ORM_HAVE_SQLITE)
    if(SQLite3_FOUND)
        target_link_libraries(c-orm PUBLIC SQLite::SQLite3)
    endif()
endif()

if(C_ORM_BACKEND_POSTGRES)
    target_compile_definitions(c-orm PUBLIC C_ORM_HAVE_POSTGRES)
    if(PostgreSQL_FOUND)
        target_link_libraries(c-orm PUBLIC PostgreSQL::PostgreSQL)
    endif()
endif()

if(C_ORM_BACKEND_MYSQL)
    target_compile_definitions(c-orm PUBLIC C_ORM_HAVE_MYSQL)
endif()

# Instead of target_link_libraries to cdd-c executable, we add include directories
target_include_directories(c-orm PUBLIC ${cdd_c_SOURCE_DIR}/include)

# Setup Testing using greatest.h just like c-abstract-http
enable_testing()
add_subdirectory(tests)
