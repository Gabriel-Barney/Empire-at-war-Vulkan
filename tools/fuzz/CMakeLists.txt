add_executable(alamo_fuzz_smoke smoke/fuzz_smoke.cpp smoke/fuzz_targets.cpp)
target_link_libraries(alamo_fuzz_smoke PRIVATE alamo_core alamo_engine_flags)
foreach(_pair "TERRAIN=alamo_terrain" "UI=alamo_ui_hud" "ANIM=alamo_anim" "PARTICLES=alamo_render_particles"
              "LUA=alamo_lua_vm" "AUDIO=alamo_audio" "VIDEO=alamo_video" "VFS=alamo_vfs")
  string(REPLACE "=" ";" _kv "${_pair}")
  list(GET _kv 0 _flag)
  list(GET _kv 1 _lib)
  if(TARGET ${_lib})
    target_link_libraries(alamo_fuzz_smoke PRIVATE ${_lib})
    target_compile_definitions(alamo_fuzz_smoke PRIVATE ALAMO_FUZZ_HAVE_${_flag}=1)
  endif()
endforeach()
add_test(NAME alamo_fuzz_smoke
         COMMAND alamo_fuzz_smoke --seconds 6 --out ${CMAKE_CURRENT_BINARY_DIR}/fuzz_smoke)
set_tests_properties(alamo_fuzz_smoke PROPERTIES LABELS slow TIMEOUT 600
                     PASS_REGULAR_EXPRESSION "FUZZ_SMOKE PASS")
