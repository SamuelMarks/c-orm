if(EXISTS "${CDD_SRC_DIR}/src/routes/parse/sync.c")
    file(READ "${CDD_SRC_DIR}/src/routes/parse/sync.c" CONTENT)
    string(REPLACE "static cdd_c_error_t make_cdd_tmpfile(FILE **out_file) {
  FILE *f = NULL;
  if (!out_file)"
                   "static cdd_c_error_t make_cdd_tmpfile(FILE **out_file) {
#if !defined(__wasm__) && !defined(__wasm32__)
  FILE *f = NULL;
#endif
  if (!out_file)"
                   CONTENT "${CONTENT}")
    file(WRITE "${CDD_SRC_DIR}/src/routes/parse/sync.c" "${CONTENT}")
endif()
