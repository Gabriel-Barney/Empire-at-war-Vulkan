# One slow synthetic test for tools/test_slots.py concurrent: two dev.cmd suite runs of it, started at once, must take
# turns on one slot. A separate project: never part of the engine build or a suite.
cmake_minimum_required(VERSION 3.28)
project(alamo_test_slots_fixture NONE)
enable_testing()
add_test(NAME alamo_slot_fixture COMMAND ${CMAKE_COMMAND} -E sleep 2)
set_tests_properties(alamo_slot_fixture PROPERTIES LABELS synthetic)
