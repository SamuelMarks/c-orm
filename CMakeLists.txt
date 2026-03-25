cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(c-orm VERSION 0.1.0 LANGUAGES C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Step 278: Static Analysis Integration
option(C_ORM_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
if(C_ORM_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_C_CLANG_TIDY ${CLANG_TIDY_EXE} "-checks=*,-readability-identifier-length,-misc-unused-parameters,-clang-analyzer-security.insecureAPI.strcpy,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling")
    else()
        message(WARNING "clang-tidy not found. Static analysis disabled.")
    endif()
endif()

# Strict warnings
if(MSVC)
    set(C_ORM_STRICT_OPTIONS /W4 /sdl)
else()
    set(C_ORM_STRICT_OPTIONS -Wall -Wextra  -Wpedantic -std=c90)
endif()

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_FULLY_DISCONNECTED OFF)

set(C_REST_FRAMEWORK_FETCH_C_FS OFF CACHE BOOL "Disable c_fs fetch in c_rest_framework")
set(C_REST_FRAMEWORK_FETCH_C_ORM OFF CACHE BOOL "Disable c_orm fetch in c_rest_framework")
set(C_REST_FRAMEWORK_FETCH_C_ABSTRACT_HTTP OFF CACHE BOOL "Disable c_abstract_http fetch")
set(C_REST_FRAMEWORK_FETCH_CDD_C OFF CACHE BOOL "Disable cdd_c fetch in c_rest_framework")

FetchContent_Declare(
    cfs
    URL https://github.com/SamuelMarks/c-fs/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(cfs)

FetchContent_Declare(
    c_abstract_http
    URL https://github.com/SamuelMarks/c-abstract-http/archive/refs/heads/master.zip
    
)
FetchContent_MakeAvailable(c_abstract_http)



FetchContent_Declare(
    cdd_c
    URL https://github.com/SamuelMarks/cdd-c/archive/refs/heads/master.zip
)
set(c_orm_SOURCE_DIR ${CMAKE_SOURCE_DIR})
FetchContent_MakeAvailable(cdd_c)


add_library(c-orm
    src/c_orm_db.c
    src/c_orm_string_builder.c
    src/c_orm_query_builder.c
    src/c_orm_api.c
    src/c_orm_sqlite.c
    src/c_orm_postgres.c
    src/c_orm_mysql.c
    src/c_orm_oauth2.c
    src/c_orm_memory.c
    src/c_orm_codegen.c
    src/c_orm_uuid.c
)
set_target_properties(c-orm PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_options(c-orm PRIVATE ${C_ORM_STRICT_OPTIONS})

target_include_directories(c-orm SYSTEM PUBLIC include)
target_link_libraries(c-orm PUBLIC c_cdd)
target_include_directories(c-orm PUBLIC ${cdd_c_SOURCE_DIR}/src ${cdd_c_BINARY_DIR}/src)

target_compile_definitions(c-orm PUBLIC $<$<BOOL:${BUILD_SHARED_LIBS}>:C_ORM_SHARED>)

# Step 211: Separate c-orm-async library
add_library(c-orm-async src/c_orm_async.c)
set_target_properties(c-orm-async PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_options(c-orm-async PRIVATE ${C_ORM_STRICT_OPTIONS})
target_include_directories(c-orm-async SYSTEM PUBLIC include)
target_link_libraries(c-orm-async PUBLIC c-orm)
target_compile_definitions(c-orm-async PUBLIC $<$<BOOL:${BUILD_SHARED_LIBS}>:C_ORM_SHARED>)

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
            FetchContent_MakeAvailable(sqlite3)
            if(NOT TARGET sqlite3)
                add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
                target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
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


option(C_ORM_BUILD_TESTS "Build tests" OFF)
option(C_ORM_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(C_ORM_BUILD_EXAMPLES "Build examples" OFF)

if(C_ORM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/e2e)
endif()


if(C_ORM_BUILD_BENCHMARKS)
# Steps 216-218: Benchmarking suite
if(WIN32 AND BUILD_SHARED_LIBS AND TARGET c89stringutils AND TARGET cfs)
    set(CDD_ENV_CMD ${CMAKE_COMMAND} -E env "PATH=$<TARGET_FILE_DIR:c89stringutils>;$<TARGET_FILE_DIR:cfs>;$ENV{PATH}")
else()
    set(CDD_ENV_CMD)
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e)

set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/tests/e2e/Models.c PROPERTIES GENERATED TRUE)

add_executable(c_orm_benchmarks tests/benchmarks/test_benchmarks.c ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e/Models.c)
add_dependencies(c_orm_benchmarks generate_e2e_models)
target_include_directories(c_orm_benchmarks PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e
    ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e/test_downloads
)
if(MSVC)
    target_compile_definitions(c_orm_benchmarks PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(c_orm_benchmarks PRIVATE c-orm)
endif()

if(C_ORM_BUILD_EXAMPLES)
# Steps 282-283: Sample applications
add_executable(c_orm_example_blog examples/blog.c)
target_link_libraries(c_orm_example_blog PRIVATE c-orm)
if(MSVC)
    target_compile_definitions(c_orm_example_blog PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(c_orm_example_dashboard examples/dashboard.c)
target_link_libraries(c_orm_example_dashboard PRIVATE c-orm c_cdd)
if(MSVC)
    target_compile_definitions(c_orm_example_dashboard PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
endif()

# Steps 287-290: Packaging and Distribution
set(CPACK_PACKAGE_NAME "c-orm")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Automated Object Relational Mapping for C89")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_CONTACT "Samuel Marks")

include(GNUInstallDirs)
install(TARGETS c-orm c-orm-async
    EXPORT c-orm-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# pkg-config (.pc) generation
set(PKG_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/c-orm.pc")
file(WRITE ${PKG_CONFIG_FILE} "prefix=${CMAKE_INSTALL_PREFIX}\n")
file(APPEND ${PKG_CONFIG_FILE} "exec_prefix=\${prefix}\n")
file(APPEND ${PKG_CONFIG_FILE} "libdir=\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}\n")
file(APPEND ${PKG_CONFIG_FILE} "includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}\n\n")
file(APPEND ${PKG_CONFIG_FILE} "Name: c-orm\n")
file(APPEND ${PKG_CONFIG_FILE} "Description: ${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n")
file(APPEND ${PKG_CONFIG_FILE} "Version: ${CPACK_PACKAGE_VERSION}\n")
file(APPEND ${PKG_CONFIG_FILE} "Libs: -L\${libdir} -lc-orm -lc-orm-async\n")
file(APPEND ${PKG_CONFIG_FILE} "Cflags: -I\${includedir}\n")
install(FILES ${PKG_CONFIG_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# .deb, .rpm generators
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.14)")
set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.14")
include(CPack)


