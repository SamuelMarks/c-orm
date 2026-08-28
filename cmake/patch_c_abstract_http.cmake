file(READ "${SRC}/src/http_types.c" CONTENT)
string(REPLACE "static enum c_abstract_http_error c_abstract_http_strdup" "enum c_abstract_http_error c_abstract_http_strdup" CONTENT "${CONTENT}")
file(WRITE "${SRC}/src/http_types.c" "${CONTENT}")

file(READ "${SRC}/include/c_abstract_http/no_discard.h" NODISC_CONTENT)
string(REPLACE "_Check_return_" "" NODISC_CONTENT "${NODISC_CONTENT}")
file(WRITE "${SRC}/include/c_abstract_http/no_discard.h" "${NODISC_CONTENT}")

file(READ "${SRC}/src/http_wininet.c" WININET_CONTENT)
string(REPLACE "#ifdef _WIN32" "#if defined(_WIN32) || defined(__CYGWIN__)" WININET_CONTENT "${WININET_CONTENT}")
string(REPLACE "#include <wininet.h>" "#include <windows.h>\n#include <wininet.h>" WININET_CONTENT "${WININET_CONTENT}")
file(WRITE "${SRC}/src/http_wininet.c" "${WININET_CONTENT}")

file(READ "${SRC}/src/http_winhttp.c" WINHTTP_CONTENT)
string(REPLACE "#ifdef _WIN32" "#if defined(_WIN32) || defined(__CYGWIN__)" WINHTTP_CONTENT "${WINHTTP_CONTENT}")
string(REPLACE "#include <winhttp.h>" "#include <windows.h>\n#include <winhttp.h>" WINHTTP_CONTENT "${WINHTTP_CONTENT}")
file(WRITE "${SRC}/src/http_winhttp.c" "${WINHTTP_CONTENT}")
