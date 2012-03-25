cmake_minimum_required (VERSION 2.8)

project (Mjolnir C)
add_executable (mjolnir mjolnir.c)
target_link_libraries (mjolnir m)
install (TARGETS mjolnir RUNTIME DESTINATION bin)
