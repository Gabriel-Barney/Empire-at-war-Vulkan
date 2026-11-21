# Development

Build the C++20 engine with Windows x64 MSVC. Use `dev.cmd build [target]` and `dev.cmd test -R <pattern>` from the root. Before finishing a change, run `dev.cmd suite synthetic`, the suite hosted CI requires. When a test gains a game-data or GPU part, list it in `tests/test_suites.txt` (README.md "Test suites"). For launcher changes, run `dev.cmd launcher` and `dev.cmd launcher-test`. Setup and game-data paths are in README.md.

Keep generated files in `build/` or `launcher/target/`.
