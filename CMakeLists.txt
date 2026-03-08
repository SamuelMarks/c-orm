cmake_minimum_required(VERSION 3.15)
project(c-orm C)

# Set C standards
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(C_ORM_BACKEND_SQLITE "Enable SQLite backend" ON)
option(C_ORM_BACKEND_POSTGRES "Enable PostgreSQL backend" OFF)
option(C_ORM_BACKEND_MYSQL "Enable MySQL backend" OFF)

option(C_ORM_LTO "Enable Link-Time Optimization" OFF)
option(C_ORM_UNICODE "Enable UNICODE" OFF)
option(C_ORM_ANSI "Enable ANSI" OFF)
option(C_ORM_MULTI_THREADED "Multi-threaded" ON)
option(C_ORM_STATIC_CRT "Use Static CRT (/MT, /MTd)" OFF)
option(C_ORM_SHARED_CRT "Use Shared CRT (/MD, /MDd)" OFF)
option(C_ORM_RUNTIME_CHECKS_RTC1 "Enable /RTC1" OFF)
option(C_ORM_RUNTIME_CHECKS_RTCS "Enable /RTCs" OFF)
option(C_ORM_RUNTIME_CHECKS_RTCU "Enable /RTCu" OFF)
option(BUILD_SHARED_LIBS "Build shared library" OFF)

if(C_ORM_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
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

add_library(c-orm
    src/c_orm.c
    src/database.c
    src/db_codegen.c
)

target_include_directories(c-orm PUBLIC include)

if(MSVC)
    if(C_ORM_STATIC_CRT)
        set_property(TARGET c-orm PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(C_ORM_SHARED_CRT)
        set_property(TARGET c-orm PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    
    if(C_ORM_RUNTIME_CHECKS_RTC1)
        target_compile_options(c-orm PRIVATE /RTC1)
    elseif(C_ORM_RUNTIME_CHECKS_RTCS)
        target_compile_options(c-orm PRIVATE /RTCs)
    elseif(C_ORM_RUNTIME_CHECKS_RTCU)
        target_compile_options(c-orm PRIVATE /RTCu)
    endif()
endif()

if(C_ORM_UNICODE)
    target_compile_definitions(c-orm PUBLIC UNICODE _UNICODE)
elseif(C_ORM_ANSI)
    target_compile_definitions(c-orm PUBLIC _MBCS)
endif()

if(NOT C_ORM_MULTI_THREADED)
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
