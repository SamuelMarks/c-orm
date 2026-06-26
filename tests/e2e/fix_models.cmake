file(READ "${CMAKE_CURRENT_BINARY_DIR}/Models.h" models_h)
string(REGEX REPLACE "#include[ \t]*<stdbool\\.h>" "/* #include <stdbool.h> */" models_h "${models_h}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Models.h" "${models_h}")

file(READ "${CMAKE_CURRENT_BINARY_DIR}/Models.c" models_c)
string(REGEX REPLACE "#include[ \t]*<stdbool\\.h>" "/* #include <stdbool.h> */" models_c "${models_c}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Models.c" "${models_c}")
