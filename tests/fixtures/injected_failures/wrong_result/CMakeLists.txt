# Injected wrong result (tools/injected_failures.py wrong-result). A separate project: never part of a suite.
# Each test prints an answer and passes only if it is 42; the wrong one prints 41.
cmake_minimum_required(VERSION 3.28)
project(alamo_injected_wrong_result NONE)
enable_testing()
add_test(NAME alamo_injected_right_result COMMAND ${CMAKE_COMMAND} -E echo "answer=42")
add_test(NAME alamo_injected_wrong_result COMMAND ${CMAKE_COMMAND} -E echo "answer=41")
set_tests_properties(alamo_injected_right_result alamo_injected_wrong_result PROPERTIES
                     LABELS synthetic PASS_REGULAR_EXPRESSION "^answer=42")
