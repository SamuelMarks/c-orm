file(READ "${SRC}/src/http_types.c" CONTENT)
string(REPLACE "static enum c_abstract_http_error c_abstract_http_strdup" "enum c_abstract_http_error c_abstract_http_strdup" CONTENT "${CONTENT}")
file(WRITE "${SRC}/src/http_types.c" "${CONTENT}")
