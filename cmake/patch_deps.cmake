file(READ "${SRC}/CMakeLists.txt" CONTENT)
string(REPLACE ";-WX;" ";" CONTENT "${CONTENT}")
string(REPLACE "/WX" "" CONTENT "${CONTENT}")
file(WRITE "${SRC}/CMakeLists.txt" "${CONTENT}")
