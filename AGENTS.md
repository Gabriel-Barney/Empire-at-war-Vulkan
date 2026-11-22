# Development

Build the C++20 engine with Windows x64 MSVC through `dev.cmd`, the one development command (`dev.cmd help`). Use `dev.cmd build [target]`, `dev.cmd check --target <target>` and `dev.cmd test -R <pattern>` from the root. Before finishing a change, run `dev.cmd check`: it builds with warnings as errors and runs the synthetic suite, which hosted CI requires. When a check fails, start from the first diagnostic and the log it names. When a test gains a game-data or GPU part, list it in `tests/test_suites.txt` (README.md "Test suites") and record the local `dev.cmd suite retail` or `vulkan` run on the pull request. For launcher changes, run `dev.cmd launcher` and `dev.cmd launcher-test`. Setup and game-data paths are in README.md.

Keep generated files in `build/` or `launcher/target/`.

Issues, labels and domain docs: `docs/agents/issue-tracker.md`, `docs/agents/triage-labels.md`, `docs/agents/domain.md`. Standing decisions are in `docs/DECISIONS.md`.
