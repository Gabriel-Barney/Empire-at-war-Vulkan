# #338: alamo_headless with its scenario, save, orders, fingerprint and result paths in a folder named outside the ANSI code
# page (Hebrew and CJK, which no single code page holds). CMake passes the arguments wide (CreateProcessW), so they reach
# alamo_headless as UTF-8 (core host_args_utf8) and every file call opens those names. No game data needed (the synthetic
# bench scenario of check_bench.cmake).
#   cmake -DEXE=<alamo_headless> -DWORK=<dir> -P check_unicode_paths.cmake
cmake_minimum_required(VERSION 3.20)
foreach(v EXE WORK)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "check_unicode_paths.cmake: -D${v}= is required")
  endif()
endforeach()
set(dir "${WORK}/אב_中文")
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${dir})

function(run out_var)
  execute_process(COMMAND ${EXE} ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message(STATUS "alamo_headless ${ARGN}: exit ${rc}")
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "expected exit 0, got ${rc}\n${out}\n${err}")
  endif()
  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

run(out --bench-make ${dir}/syn.abench --save-session synthetic --bench-save-at 30 --ticks 30 --workers 0 --quiet)
foreach(f syn.abench syn.alsv syn.acmd syn.crc)
  if(NOT EXISTS ${dir}/${f})
    message(FATAL_ERROR "--bench-make wrote no ${dir}/${f}:\n${out}")
  endif()
endforeach()
run(out --bench ${dir}/syn.abench --runs 1 --warmup 0 --bench-json ${dir}/結果.json)
string(FIND "${out}" "bench: OK" ok)
if(ok EQUAL -1)
  message(FATAL_ERROR "expected 'bench: OK' in:\n${out}")
endif()
if(NOT EXISTS ${dir}/結果.json)
  message(FATAL_ERROR "--bench-json wrote no ${dir}/結果.json")
endif()
file(REMOVE_RECURSE ${WORK})
