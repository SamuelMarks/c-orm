cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(c-orm VERSION 0.1.0 LANGUAGES C)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.10" CACHE STRING "Minimum OS X deployment version" FORCE)
endif()
string(TOUPPER "${PROJECT_NAME}" PROJECT_NAME_UPPER)
string(REPLACE "-" "_" PROJECT_NAME_SAFE "${PROJECT_NAME_UPPER}")

set(${PROJECT_NAME_SAFE}_CRT_LINKAGE "dynamic" CACHE STRING "CRT Linkage (static/dynamic)")
set_property(CACHE ${PROJECT_NAME_SAFE}_CRT_LINKAGE PROPERTY STRINGS "static" "dynamic")
set(${PROJECT_NAME_SAFE}_CHARSET "UNICODE" CACHE STRING "Character set (UNICODE/ANSI)")
set_property(CACHE ${PROJECT_NAME_SAFE}_CHARSET PROPERTY STRINGS "UNICODE" "ANSI")
option(${PROJECT_NAME_SAFE}_THREADING "Enable multi-threading" ON)
set(${PROJECT_NAME_SAFE}_MSVC_RTC "OFF" CACHE STRING "MSVC Runtime Checks (OFF, 1, s, u)")
set_property(CACHE ${PROJECT_NAME_SAFE}_MSVC_RTC PROPERTY STRINGS "OFF" "1" "s" "u")
option(${PROJECT_NAME_SAFE}_LTO "Enable Link-Time Optimization (IPO)" OFF)

if(${PROJECT_NAME_SAFE}_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

# Pass configurations down to dependencies
set(CDD_CHARSET ${${PROJECT_NAME_SAFE}_CHARSET} CACHE STRING "Character set for cdd" FORCE)
set(CDD_THREADING ${${PROJECT_NAME_SAFE}_THREADING} CACHE BOOL "Threading for cdd" FORCE)
set(CDD_MSVC_RTC ${${PROJECT_NAME_SAFE}_MSVC_RTC} CACHE STRING "MSVC RTC for cdd" FORCE)

if(MSVC)
    if(${PROJECT_NAME_SAFE}_CRT_LINKAGE STREQUAL "static")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(C_CDD_STATIC_CRT ON CACHE BOOL "" FORCE)
    elseif(${PROJECT_NAME_SAFE}_CRT_LINKAGE STREQUAL "dynamic")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set(C_CDD_STATIC_CRT OFF CACHE BOOL "" FORCE)
    elseif(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
        set(C_CDD_STATIC_CRT ON CACHE BOOL "" FORCE)
    endif()
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON CACHE INTERNAL "Skip system runtime libs")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_compile_definitions(C_ORM_TEST_ALLOCATOR)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(MSVC AND MSVC_VERSION LESS 1600)
    file(GLOB MSVC_DEBUG_CRT "$ENV{VCINSTALLDIR}/redist/Debug_NonRedist/x86/Microsoft.VC80.DebugCRT/*")
    if(MSVC_DEBUG_CRT)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        file(COPY ${MSVC_DEBUG_CRT} DESTINATION "${CMAKE_BINARY_DIR}/bin")
    endif()
endif()

if(MSVC)
endif()

# Step 278: Static Analysis Integration
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
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
    set(C_ORM_STRICT_OPTIONS /W4 /WX /sdl)
elseif(CMAKE_C_COMPILER_ID MATCHES "Watcom")
    set(C_ORM_STRICT_OPTIONS -w4 -we)
else()
    set(C_ORM_STRICT_OPTIONS -Wall -Wextra -pedantic -Werror -std=c90 -Wdeclaration-after-statement -Wmissing-prototypes)
endif()

if(EMSCRIPTEN)
    list(APPEND C_ORM_STRICT_OPTIONS -pthread)
    add_link_options(-pthread "SHELL:-s USE_PTHREADS=1" "SHELL:-s STACK_SIZE=5MB" "SHELL:-s EMULATE_FUNCTION_POINTER_CASTS=1" -lidbfs.js)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
    set(HTTP_ONLY ON CACHE BOOL "Only build HTTP client/server (no OpenSSL)" FORCE)
    add_compile_definitions(CDD_NO_UCONTEXT=1)
    # Replace -std=c90 with -std=gnu90 to support EM_ASM extensions
    string(REPLACE "-std=c90" "-std=gnu90" C_ORM_STRICT_OPTIONS "${C_ORM_STRICT_OPTIONS}")
endif()

include(FetchContent)
set(OLD_BUILD_TESTING_VAL "${BUILD_TESTING}")
#set(BUILD_TESTING OFF)




if(NOT WIN32 AND NOT EMSCRIPTEN)
if(TARGET curl OR TARGET CURL::libcurl OR TARGET libcurl)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../curl/CMakeLists.txt")
    FetchContent_Declare(curl SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../curl")
    FetchContent_MakeAvailable(curl)
        set(CURL_FOUND ON)
        set(curl_FOUND ON)
else()
    if(CDD_DEPS STREQUAL "VCPKG" OR (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg") OR DEFINED VCPKG_TOOLCHAIN)
        find_package(CURL CONFIG QUIET)
    endif()
    if(NOT WIN32 AND NOT CURL_FOUND AND NOT TARGET CURL::libcurl)
        find_package(CURL QUIET)
    endif()
    if(NOT WIN32 AND NOT CURL_FOUND AND NOT TARGET CURL::libcurl)
        set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
        set(CURL_ZLIB OFF CACHE INTERNAL "")
        set(USE_LIBIDN2 OFF CACHE INTERNAL "")
        set(CURL_USE_LIBPSL OFF CACHE INTERNAL "")
        set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "")
        set(CURL_DISABLE_TESTS ON CACHE INTERNAL "")
        set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
        set(ENABLE_CURL_MANUAL OFF CACHE INTERNAL "")
        set(BUILD_LIBCURL_DOCS OFF CACHE INTERNAL "")
        FetchContent_Declare(curl URL https://github.com/curl/curl/archive/refs/tags/curl-8_8_0.tar.gz)
        FetchContent_MakeAvailable(curl)
        set(CURL_FOUND ON)
        set(curl_FOUND ON)
    endif()
    if(TARGET curl AND NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS curl)
    elseif(TARGET libcurl AND NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    endif()
endif()
endif()


set(OLD_BUILD_TESTING_VAL "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(C_CDD_BUILD_TESTS OFF)
set(C_FS_BUILD_TESTS OFF)
set(C_FS_BUILD_TESTING OFF)
set(C_ABSTRACT_HTTP_BUILD_TESTING OFF)
set(CDD_BUILD_TESTS OFF)

if(TARGET c_str_span)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../c-str-span/CMakeLists.txt")
    FetchContent_Declare(c_str_span SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../c-str-span")
    FetchContent_MakeAvailable(c_str_span)
else()
    FetchContent_Declare(c_str_span GIT_REPOSITORY https://github.com/SamuelMarks/c-str-span.git GIT_TAG master)
    FetchContent_MakeAvailable(c_str_span)
endif()

if(TARGET c89stringutils)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../c89stringutils/CMakeLists.txt")
    FetchContent_Declare(c89stringutils EXCLUDE_FROM_ALL SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../c89stringutils")
    FetchContent_MakeAvailable(c89stringutils)
else()
    FetchContent_Declare(c89stringutils EXCLUDE_FROM_ALL GIT_REPOSITORY https://github.com/offscale/c89stringutils.git GIT_TAG master)
    FetchContent_MakeAvailable(c89stringutils)
endif()

set(C_REST_FRAMEWORK_FETCH_C_FS OFF CACHE BOOL "Disable c_fs fetch in c_rest_framework")
set(C_REST_FRAMEWORK_FETCH_C_ORM OFF CACHE BOOL "Disable c_orm fetch in c_rest_framework")
set(C_REST_FRAMEWORK_FETCH_CDD_C OFF CACHE BOOL "Disable cdd_c fetch in c_rest_framework")

if(TARGET cfs)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../c-fs/CMakeLists.txt")
    FetchContent_Declare(cfs SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../c-fs")
    FetchContent_MakeAvailable(cfs)
else()
    FetchContent_Declare(cfs GIT_REPOSITORY https://github.com/SamuelMarks/c-fs.git GIT_TAG master)
    FetchContent_MakeAvailable(cfs)
endif()

if(TARGET parson)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../parson/CMakeLists.txt")
    FetchContent_Declare(parson SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../parson")
    FetchContent_MakeAvailable(parson)
    if(TARGET parson AND NOT TARGET parson::parson)
        add_library(parson::parson ALIAS parson)
    endif()
else()
    FetchContent_Declare(parson GIT_REPOSITORY https://github.com/SamuelMarks/parson.git GIT_TAG master)
    FetchContent_MakeAvailable(parson)
    if(TARGET parson AND NOT TARGET parson::parson)
        add_library(parson::parson ALIAS parson)
    endif()
    if(TARGET parson AND parson_SOURCE_DIR)
        target_include_directories(parson PUBLIC $<BUILD_INTERFACE:${parson_SOURCE_DIR}>)
    endif()
endif()

if(TARGET c-abstract-http)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../c-abstract-http/CMakeLists.txt")
    FetchContent_Declare(c-abstract-http SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../c-abstract-http")
    FetchContent_MakeAvailable(c-abstract-http)
else()
    FetchContent_Declare(c-abstract-http GIT_REPOSITORY https://github.com/SamuelMarks/c-abstract-http.git GIT_TAG master)
    FetchContent_MakeAvailable(c-abstract-http)
endif()

if(TARGET c_cdd)
    # Target already populated natively
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../cdd-c/CMakeLists.txt")
    FetchContent_Declare(cdd_c SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../cdd-c")
    set(c_orm_SOURCE_DIR ${CMAKE_SOURCE_DIR})
    FetchContent_MakeAvailable(cdd_c)
else()
    FetchContent_Declare(cdd_c GIT_REPOSITORY https://github.com/SamuelMarks/cdd-c.git GIT_TAG master PATCH_COMMAND ${CMAKE_COMMAND} -DSRC=<SOURCE_DIR> -P ${CMAKE_CURRENT_LIST_DIR}/cmake/patch_cdd_c.cmake)
    set(c_orm_SOURCE_DIR ${CMAKE_SOURCE_DIR})
    FetchContent_MakeAvailable(cdd_c)
endif()

set(BUILD_TESTING "${OLD_BUILD_TESTING_VAL}")

add_library(c-orm
    src/cdd_c_compat.c
    src/c_orm_db.c
    src/c_orm_log.c
    src/c_orm_alloc.c
    src/c_orm_string_builder.c
    src/c_orm_query_builder.c
    src/c_orm_arena.c
    src/c_orm_query_fluent.c
    src/c_orm_query_sql.c
    src/c_orm_api.c
    src/c_orm_cache.c
    src/c_orm_sqlite.c
    src/c_orm_postgres.c
    src/c_orm_mysql.c
    src/c_orm_oauth2.c
    src/c_orm_memory.c
    src/c_orm_codegen.c
    src/c_orm_uuid.c
    src/c_orm_migrations.c
    src/c_orm_modality.c
      src/abstract_struct.c
      src/cdd_c_ir.c
      src/hydrate_router.c
      src/migration.c
      src/migration_runner.c
      src/orm_gen.c
      src/query_projection.c
      src/c_orm_c_to_sql.c
      src/c_orm_sql.c
      src/c_orm_sql_to_c.c

)
set_target_properties(c-orm PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_options(c-orm PRIVATE ${C_ORM_STRICT_OPTIONS})

target_include_directories(c-orm SYSTEM PUBLIC include)
target_link_libraries(c-orm PUBLIC c_cdd cfs parson c89stringutils c_str_span)
if(TARGET c_cdd)
    add_dependencies(c-orm c_cdd)
endif()
target_include_directories(c-orm SYSTEM PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../cdd-c/include>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../cdd-c/src>" "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps/cdd_c-build/src>")
if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        target_link_libraries(c-orm PUBLIC OpenSSL::Crypto)
    else()
        target_link_libraries(c-orm PUBLIC crypto)
    endif()
endif()
  target_include_directories(c-orm PUBLIC )
  target_include_directories(c-orm PUBLIC )






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

if(EMSCRIPTEN)
    set(C_ORM_POSTGRESQL "OFF" CACHE STRING "Enable PostgreSQL driver (LINKED, DYNAMIC, OFF)" FORCE)
    set(C_ORM_MYSQL "OFF" CACHE STRING "Enable MySQL driver (LINKED, DYNAMIC, OFF)" FORCE)
    set(C_ORM_SQLITE "LINKED" CACHE STRING "Enable SQLite driver (LINKED, DYNAMIC, OFF)" FORCE)
endif()

if(C_ORM_SQLITE STREQUAL "LINKED")
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
        target_link_libraries(c-orm PRIVATE unofficial::sqlite3::sqlite3)
        set(C_ORM_SQLITE_TARGET unofficial::sqlite3::sqlite3 CACHE INTERNAL "SQLite3 target name")
    else()
        find_package(SQLite3 QUIET)
        if(TARGET SQLite3::SQLite3)
            target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
            target_link_libraries(c-orm PRIVATE SQLite3::SQLite3)
            set(C_ORM_SQLITE_TARGET SQLite3::SQLite3 CACHE INTERNAL "SQLite3 target name")
        elseif(SQLite3_FOUND AND NOT SQLite3_LIBRARIES STREQUAL "")
            target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_LINKED C_ORM_ENABLE_SQLITE)
            target_include_directories(c-orm PRIVATE ${SQLite3_INCLUDE_DIRS})
            target_link_libraries(c-orm PRIVATE ${SQLite3_LIBRARIES})
            set(C_ORM_SQLITE_TARGET ${SQLite3_LIBRARIES} CACHE INTERNAL "SQLite3 target name")
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
            set(C_ORM_SQLITE_TARGET sqlite3 CACHE INTERNAL "SQLite3 target name")
        endif()
    endif()
elseif(C_ORM_SQLITE STREQUAL "DYNAMIC")
    target_compile_definitions(c-orm PUBLIC C_ORM_SQLITE_DYNAMIC C_ORM_ENABLE_SQLITE)
    if(NOT WIN32 AND NOT EMSCRIPTEN)
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
    if(NOT WIN32 AND NOT EMSCRIPTEN)
        target_link_libraries(c-orm PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(C_ORM_MYSQL STREQUAL "LINKED")
    find_package(unofficial-libmariadb CONFIG REQUIRED)
    target_compile_definitions(c-orm PUBLIC C_ORM_MYSQL_LINKED C_ORM_ENABLE_MYSQL)
    target_link_libraries(c-orm PRIVATE unofficial::libmariadb)
elseif(C_ORM_MYSQL STREQUAL "DYNAMIC")
    target_compile_definitions(c-orm PUBLIC C_ORM_MYSQL_DYNAMIC)
    if(NOT WIN32 AND NOT EMSCRIPTEN)
        target_link_libraries(c-orm PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()


if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(C_ORM_BUILD_TESTS "Build tests" ON)
    if(C_ORM_BUILD_TESTS)
        set(BUILD_TESTING ON CACHE BOOL "Build the testing tree." FORCE)
        include(CTest)
        enable_testing()
        add_subdirectory(tests/e2e)
    endif()
endif()

option(C_ORM_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(C_ORM_BUILD_EXAMPLES "Build examples" OFF)


if(C_ORM_BUILD_BENCHMARKS)
# Steps 216-218: Benchmarking suite
if(WIN32 AND BUILD_SHARED_LIBS AND TARGET c89stringutils AND TARGET cfs)
    set(CDD_ENV_CMD ${CMAKE_COMMAND} -E env "PATH=$<TARGET_FILE_DIR:c89stringutils>;$<TARGET_FILE_DIR:cfs>;$ENV{PATH}")
else()
    set(CDD_ENV_CMD)
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e)



add_executable(c_orm_benchmarks tests/benchmarks/test_benchmarks.c )
add_dependencies(c_orm_benchmarks e2e_models)
target_link_libraries(c_orm_benchmarks PRIVATE e2e_models)
target_include_directories(c_orm_benchmarks PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e
    ${CMAKE_CURRENT_BINARY_DIR}/tests/e2e/test_downloads
)
if(MSVC)

endif()
target_link_libraries(c_orm_benchmarks PRIVATE c-orm)
endif()

if(C_ORM_BUILD_EXAMPLES)
# Steps 282-283: Sample applications
add_executable(c_orm_example_blog examples/blog.c)
target_link_libraries(c_orm_example_blog PRIVATE c-orm)
if(MSVC)
endif()

add_executable(c_orm_example_relationships examples/relationships.c)
target_link_libraries(c_orm_example_relationships PRIVATE c-orm)
if(MSVC)
endif()
add_executable(c_orm_example_dashboard examples/dashboard.c)
target_link_libraries(c_orm_example_dashboard PRIVATE c-orm c_cdd)
if(MSVC)
endif()
endif()

# Steps 287-290: Packaging and Distribution
set(CPACK_PACKAGE_NAME "c-orm")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Automated Object Relational Mapping for C89")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_CONTACT "Samuel Marks")

include(GNUInstallDirs)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
# Phase 4: CLI
add_executable(c-orm-cli src/c_orm_cli.c)
target_link_libraries(c-orm-cli PRIVATE c-orm)
if(EMSCRIPTEN)
    target_link_options(c-orm-cli PRIVATE "SHELL:-s NODERAWFS=1" "SHELL:-s USE_PTHREADS=1" "SHELL:-s STACK_SIZE=5MB")
endif()
if(MSVC)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET c-orm-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:c-orm-cli> $<TARGET_FILE_DIR:c-orm-cli>
        COMMAND_EXPAND_LISTS
    )
    if(MSVC AND CMAKE_CROSSCOMPILING)
        get_filename_component(MSVC_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(MSVC_ROOT_DIR "${MSVC_BIN_DIR}/../.." ABSOLUTE)
        file(GLOB MSVC_DEBUG_CRT "${MSVC_ROOT_DIR}/VC/Redist/MSVC/*/debug_nonredist/x64/Microsoft.VC14*.DebugCRT/*.dll")
        file(GLOB MSVC_RELEASE_CRT "${MSVC_ROOT_DIR}/VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT/*.dll")
        file(GLOB MSVC_UCRT "${MSVC_ROOT_DIR}/Windows Kits/10/bin/*/x64/ucrt/*.dll")

        if(MSVC_DEBUG_CRT)
            add_custom_command(TARGET c-orm-cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MSVC_DEBUG_CRT} $<TARGET_FILE_DIR:c-orm-cli>
            )
        endif()
        if(MSVC_RELEASE_CRT)
            add_custom_command(TARGET c-orm-cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MSVC_RELEASE_CRT} $<TARGET_FILE_DIR:c-orm-cli>
            )
        endif()
        if(MSVC_UCRT)
            add_custom_command(TARGET c-orm-cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MSVC_UCRT} $<TARGET_FILE_DIR:c-orm-cli>
            )
        endif()
    elseif(MSVC)
        set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
        set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
        include(InstallRequiredSystemLibraries)
        if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            add_custom_command(TARGET c-orm-cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:c-orm-cli>
            )
        endif()
    endif()
endif()

install(TARGETS c-orm-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

endif()
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

include(CheckCCompilerFlag)
