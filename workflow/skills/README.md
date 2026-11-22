# Project skills

The workflow skills for Claude Code and Codex come from one source (`docs/GROUND_UP_WORKFLOW_DESIGN.md` "One skill
source for Claude Code and Codex").

- `upstream/` is a verbatim copy of the selected skills from [mattpocock/skills](https://github.com/mattpocock/skills)
  at the revision in `manifest.json`, with its MIT `LICENSE`. Do not edit it by hand.
- `manifest.json` lists each skill: its upstream path, its invocation policy, and the local adaptations.
  - `"invocation": "user"`: only the user starts it (`/eaw-<name>` in Claude Code, `$eaw-<name>` in Codex). Setup,
    triage, specification and ticket creation, multi-session planning, implementation entry, handoff and
    retrospectives are user-invoked.
  - `"invocation": "model"`: the agent may also start it when the task calls for it (TDD, diagnosis, design, review).
  - An adaptation is an exact `find` / `replace` in one upstream file, or `append` text. A `find` that matches zero or
    several times fails generation, so an upstream update cannot silently drop a project change.
- `tools/skills.py generate` writes `.claude/skills/eaw-*/` (Claude's `disable-model-invocation`) and
  `.agents/skills/eaw-*/` (Codex's `agents/openai.yaml` policy) as ordinary checked-in files, renaming references
  between skills to their `eaw-` names. Never edit the generated folders.
- `tools/skills.py check` (CTest `alamo_skills_check`, synthetic, so `landing` runs it) fails when a generated file
  differs from the source, a skill's metadata does not parse or disagrees with the manifest, a skill reference does
  not resolve, or a hand-made project skill takes the `eaw-` prefix.

## Change a skill

Edit `manifest.json` (an adaptation, or the invocation policy), run `python -I tools/skills.py generate`, and commit the
source and both generated trees together.

## Update from upstream

`python -I tools/skills.py vendor --revision <sha>` re-copies `upstream/` and records the revision. Then run `generate`,
fix any adaptation whose `find` no longer matches, and open a pull request with the upstream diff. Before merging,
start one user-invoked and one model-invoked skill in each client you use, and check that no personal or marketplace
skill has the same name.
