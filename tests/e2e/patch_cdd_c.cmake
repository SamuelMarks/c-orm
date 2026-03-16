file(READ "src/routes/emit/orm_gen.c" file_content)
string(REPLACE "struct_name, struct_name);\n            struct_name);" "struct_name, struct_name);" file_content "${file_content}")
string(REPLACE "const char *fk_target = \"NULL\";" "const char *fk_target = \"NULL\"; (void)fk_target;" file_content "${file_content}")
file(WRITE "src/routes/emit/orm_gen.c" "${file_content}")