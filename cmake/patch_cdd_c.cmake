file(READ "${SRC}/include/c_cdd/memory.h" CONTENT)
string(REPLACE "#ifdef CDD_BUILD_TESTS\nextern int g_cdd_alloc_fail;" "extern int g_cdd_alloc_fail;\n#ifdef CDD_BUILD_TESTS" CONTENT "${CONTENT}")
file(WRITE "${SRC}/include/c_cdd/memory.h" "${CONTENT}")

file(READ "${SRC}/src/cdd_api.c" CONTENT)
string(REGEX REPLACE "C_CDD_EXPORT int g_crypto_fail_[a-zA-Z0-9_]+ = 0;\n" "" CONTENT "${CONTENT}")
file(WRITE "${SRC}/src/cdd_api.c" "${CONTENT}")

file(READ "${SRC}/src/classes/emit/json.c" CONTENT)
string(REGEX REPLACE "C_CDD_EXPORT int g_fail_io_after = -1;\n" "extern int g_fail_io_after;\n" CONTENT "${CONTENT}")
string(REGEX REPLACE "C_CDD_EXPORT int g_io_calls = 0;\n" "extern int g_io_calls;\n" CONTENT "${CONTENT}")
file(WRITE "${SRC}/src/classes/emit/json.c" "${CONTENT}")
