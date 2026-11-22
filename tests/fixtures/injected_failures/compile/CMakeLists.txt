# Injected compile failure (tools/injected_failures.py compile). A separate project: never part of the engine build.
cmake_minimum_required(VERSION 3.28)
project(alamo_injected_compile_failure CXX)
add_library(injected_compile_failure STATIC injected_compile_failure.cpp)
