add_executable(test_together test_together.c)
target_link_libraries(test_together lace)
add_test(test_together test_together)
