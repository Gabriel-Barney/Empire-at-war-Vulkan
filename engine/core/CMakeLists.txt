add_library(alamo_core STATIC
  src/hash.cpp
  src/log.cpp
  src/file_provider.cpp
  src/galaxy.cpp
  # Q1 determinism harness: per-tick CRC registry, sim RNG, engine-owned math.
  src/sim_crc.cpp
  src/bytes.cpp
  src/sim_rng.cpp
  src/dmath.cpp
  # N1: engine save files (header + named chunks), packed with the in-house LZ77 + Huffman codec.
  src/save_file.cpp
  src/compress.cpp
  # tick profiler (wall-clock data, never read by sim logic).
  src/profile.cpp
  # atomic whole-file replace for the tool / render caches (never read by sim logic).
  src/atomic_file.cpp
  # mod compatibility issue sink (docs/MODS.md "Fault containment and the no-abort rule"; diagnostics only, never read by sim logic).
  src/compat_issue.cpp
)
add_library(alamo::core ALIAS alamo_core)
target_include_directories(alamo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alamo_core PUBLIC alamo_engine_flags Threads::Threads)

add_executable(alamo_core_tests tests/test_core.cpp)
target_link_libraries(alamo_core_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_tests COMMAND alamo_core_tests)
set_tests_properties(alamo_core_tests PROPERTIES TIMEOUT 60)  # runs in < 1 s: a hang fails fast

# Q1: StateHasher / CrcRegistry / CrcLog compare / ByteWriter / SimRng.
add_executable(alamo_core_sim_tests tests/test_sim_crc.cpp)
target_link_libraries(alamo_core_sim_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_sim_tests COMMAND alamo_core_sim_tests)
set_tests_properties(alamo_core_sim_tests PROPERTIES TIMEOUT 60)

# N1: SaveFile container + ByteWriter/Reader save helpers.
add_executable(alamo_core_save_tests tests/test_save_file.cpp)
target_link_libraries(alamo_core_save_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_save_tests COMMAND alamo_core_save_tests)
set_tests_properties(alamo_core_save_tests PROPERTIES TIMEOUT 60)

# write_file_atomic (temp file + rename) and its retry / failure results. Own temp dir per run.
add_executable(alamo_core_atomic_file_tests tests/test_atomic_file.cpp)
target_link_libraries(alamo_core_atomic_file_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_atomic_file_tests COMMAND alamo_core_atomic_file_tests)
set_tests_properties(alamo_core_atomic_file_tests PROPERTIES TIMEOUT 60)

# IssueSink de-duplication, counts and index-order merge.
add_executable(alamo_core_compat_issue_tests tests/test_compat_issue.cpp)
target_link_libraries(alamo_core_compat_issue_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_compat_issue_tests COMMAND alamo_core_compat_issue_tests)
set_tests_properties(alamo_core_compat_issue_tests PROPERTIES TIMEOUT 60)

# Q1: dmath accuracy vs the CRT. Bit-identical outputs across commits: the scenario dmath of
# tools/goldens/scenarios.json (--write-golden <file>; no stored digests).
add_executable(alamo_core_dmath_tests tests/test_dmath.cpp)
target_link_libraries(alamo_core_dmath_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_dmath_tests
         COMMAND alamo_core_dmath_tests)
set_tests_properties(alamo_core_dmath_tests PROPERTIES TIMEOUT 120)

# Q1: sim modules must not call CRT transcendentals (use alamo/core/dmath.hpp).
add_test(NAME alamo_core_sim_math_lint
         COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${PROJECT_SOURCE_DIR}/engine
                 -P ${PROJECT_SOURCE_DIR}/tools/lint/check_sim_math.cmake)
# The lint must reject its fixture (std::atan2 in a fake galactic/src file).
add_test(NAME alamo_core_sim_math_lint_selftest
         COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${PROJECT_SOURCE_DIR}/tools/lint/fixture
                 -P ${PROJECT_SOURCE_DIR}/tools/lint/check_sim_math.cmake)
set_tests_properties(alamo_core_sim_math_lint_selftest PROPERTIES WILL_FAIL TRUE)

# no mod / faction name literals in engine/ sim code (tools/lint/check_mod_names.py, legacy docs/status/modc-audit.md). The
# self-test checks the lint against its fixture (tools/lint/fixture_mod_names). Python: the project venv first, as the
# golden checks. Without python these lints are not registered, and tools/test_suite.py fails a synthetic run because
# tests/required_synthetic.txt lists them.
find_program(ALAMO_LINT_PYTHON NAMES python PATHS ${PROJECT_SOURCE_DIR}/toolchain/venv/Scripts NO_DEFAULT_PATH)
find_program(ALAMO_LINT_PYTHON NAMES python python3)
if(ALAMO_LINT_PYTHON)
  add_test(NAME alamo_core_mod_names_lint
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_mod_names.py --engine ${PROJECT_SOURCE_DIR}/engine)
  add_test(NAME alamo_core_mod_names_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_mod_names.py --selftest)
  set_tests_properties(alamo_core_mod_names_lint alamo_core_mod_names_lint_selftest PROPERTIES TIMEOUT 120)
  # every integer limit in sim code carries a // limit: retail|data|dynamic|guard tag (tools/lint/check_limits.py,
  # docs/MODS.md "Limits policy").
  add_test(NAME alamo_core_limits_lint
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_limits.py --engine ${PROJECT_SOURCE_DIR}/engine)
  add_test(NAME alamo_core_limits_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_limits.py --selftest)
  set_tests_properties(alamo_core_limits_lint alamo_core_limits_lint_selftest PROPERTIES TIMEOUT 120)
  # no Vfs::mod_dirs() call outside engine/vfs, no modinfo.json reader outside the mod catalog, no removed shim
  # name (tools/lint/check_mod_system.py, docs/MODS.md "File resolution order").
  add_test(NAME alamo_core_mod_system_lint
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_mod_system.py --root ${PROJECT_SOURCE_DIR})
  add_test(NAME alamo_core_mod_system_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lint/check_mod_system.py --selftest)
  set_tests_properties(alamo_core_mod_system_lint alamo_core_mod_system_lint_selftest PROPERTIES TIMEOUT 120)
  # Doc references: every cited docs/...md exists or is listed in docs/LEGACY_DOCS.md (tools/lint/check_doc_refs.py).
  add_test(NAME alamo_core_doc_refs_lint
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_doc_refs.py --root ${PROJECT_SOURCE_DIR})
  add_test(NAME alamo_core_doc_refs_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_doc_refs.py --selftest)
  # The dev.cmd implementation's own unit tests (diagnostic extraction, presets, argument handling).
  add_test(NAME alamo_dev_selftest COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/dev.py selftest)
  # Project skills: .claude/skills and .agents/skills match workflow/skills, their metadata parses, every skill
  # reference resolves and no name is taken twice (tools/skills.py; docs/GROUND_UP_WORKFLOW_DESIGN.md).
  add_test(NAME alamo_skills_check COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/skills.py check)
  add_test(NAME alamo_skills_selftest COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/skills.py selftest)
  # The landing gate's path classification and verdict (tools/landing_gate.py; .github/workflows/landing.yml).
  add_test(NAME alamo_landing_gate_selftest COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/landing_gate.py selftest)
  # The nightly analyze check's baseline comparison and the nightly record (tools/analyze_report.py,
  # tools/nightly_record.py; .github/workflows/nightly.yml).
  add_test(NAME alamo_analyze_report_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/analyze_report.py selftest)
  add_test(NAME alamo_nightly_record_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/nightly_record.py selftest)
  # The exact-package smoke test's checksum and manifest verification (tools/package_smoke.py; README.md "Internal
  # packages").
  add_test(NAME alamo_package_smoke_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/package_smoke.py selftest)
  set_tests_properties(alamo_dev_selftest alamo_landing_gate_selftest alamo_skills_check alamo_skills_selftest
                       alamo_analyze_report_selftest alamo_nightly_record_selftest alamo_package_smoke_selftest
                       PROPERTIES TIMEOUT 120)
  # Temporary: the two-PR race demonstration's invariant (tools/race_demo.py; tests/fixtures/race_demo/README.md, #17).
  add_test(NAME alamo_race_demo COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/race_demo.py)
  # Machine-wide test slots: a second job waits for the holder, names it, and gets the slot when the holder exits or
  # crashes (tools/test_slots.py; README.md "Test slots"). It uses its own slot folder, not the machine's.
  add_test(NAME alamo_test_slots_selftest COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/test_slots.py selftest)
  # Two real dev.cmd suite runs started at once on one build tree (tests/fixtures/test_slots): one waits and names
  # the other, and each keeps its own temp folder, report and log.
  add_test(NAME alamo_test_slots_concurrent
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/test_slots.py concurrent)
  set_tests_properties(alamo_test_slots_selftest alamo_test_slots_concurrent PROPERTIES TIMEOUT 300)
  # The suite runner's own checks (suite labels, retry policy, skip reasons, the report table).
  add_test(NAME alamo_test_suite_selftest COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/test_suite.py --selftest)
  # Injected failures fail at the intended point (tools/injected_failures.py; tests/fixtures/injected_failures): a
  # compile error fails dev.cmd at the build step with the compiler's diagnostic, a wrong result fails the suite run.
  add_test(NAME alamo_injected_compile_failure
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/injected_failures.py compile)
  add_test(NAME alamo_injected_wrong_result
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/injected_failures.py wrong-result)
  set_tests_properties(alamo_test_suite_selftest PROPERTIES TIMEOUT 120)
  set_tests_properties(alamo_injected_compile_failure alamo_injected_wrong_result PROPERTIES TIMEOUT 300)
  set_tests_properties(alamo_core_doc_refs_lint alamo_core_doc_refs_lint_selftest PROPERTIES TIMEOUT 120)
  # sim code turns data-driven seconds into frame counts only through alamo/core/frames.hpp (no plain
  # static_cast of seconds x FPS to an integer outside engine/core; tools/lint/check_frame_casts.py).
  add_test(NAME alamo_core_frame_casts_lint
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_frame_casts.py --root ${PROJECT_SOURCE_DIR})
  add_test(NAME alamo_core_frame_casts_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_frame_casts.py --selftest)
  set_tests_properties(alamo_core_frame_casts_lint alamo_core_frame_casts_lint_selftest PROPERTIES TIMEOUT 120)
else()
  message(WARNING "python not found: the lint tests (alamo_core_mod_names_lint, ...) are not registered; dev.cmd suite "
                  "synthetic will report them missing. Run toolchain\\2_setup.cmd to create toolchain\\venv.")
endif()

# profiler zones / laps / ring / report.
add_executable(alamo_core_profile_tests tests/test_profile.cpp)
target_link_libraries(alamo_core_profile_tests PRIVATE alamo_core alamo_test_support)
add_test(NAME alamo_core_profile_tests COMMAND alamo_core_profile_tests)
set_tests_properties(alamo_core_profile_tests PROPERTIES TIMEOUT 60)
