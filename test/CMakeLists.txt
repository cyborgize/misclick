add_executable(misclick_test misclick_test.c)
target_link_libraries(misclick_test PRIVATE misclick)
add_test(NAME misclick_test COMMAND misclick_test)
