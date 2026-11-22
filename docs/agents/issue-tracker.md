# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues in `Gabriel-Barney/Empire-at-war-Vulkan`. Use the `gh` CLI for
all operations. Adapted from the `setup-matt-pocock-skills` seed (`mattpocock/skills`, MIT).

## The issue owns the outcome

One issue owns each outcome (`docs/GROUND_UP_WORKFLOW_DESIGN.md` "Work intake and ownership"). Its brief states:

- the outcome, with one or two observable acceptance examples;
- the test seam (a CTest entry, headless scenario or fixture) or the manual evidence;
- blockers and risk, including whether it touches a simulation contract, save format, build gate or release.

The assignee owns the next action. The branch, the code and the last checked commit live on the linked pull request.
Before switching clients, put the last passing or failing command and the next step on the issue or draft PR.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --json number,title,body,labels,comments`.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with `--label` and `--state` filters.
- **Sub-issue of a parent**: `gh issue create --parent <parent> ...`, or `gh issue edit <parent> --add-sub-issue <child>`. Without sub-issues, put `Part of #<parent>` at the top of the child body.
- **Blocking**: GitHub's native issue dependencies. `gh issue create --blocked-by <n>,<n> ...` or `gh issue edit <child> --add-blocked-by <blocker>`. Where dependencies are unavailable, put a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Count open blockers**: `gh api repos/Gabriel-Barney/Empire-at-war-Vulkan/issues/<n> --jq .issue_dependencies_summary.blocked_by` (0 means startable).
- **Comment**: `gh issue comment <number> --body "..."`.
- **Labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."` (names in `docs/agents/triage-labels.md`).
- **Close**: post the explanation with `gh issue comment`, then `gh issue close <number>`.

File issues only into this repository, never into `mattpocock/skills`.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads
this flag.)_

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Read it as in **Read an issue** above.

## Wayfinding operations

Used by `/wayfinder`. The map is one issue labelled `wayfinder:map` (Notes / Decisions-so-far / Fog); its child issues
are sub-issues labelled `wayfinder:research`, `wayfinder:prototype`, `wayfinder:grilling` or `wayfinder:task`. Claim a
child with `gh issue edit <n> --add-assignee @me`; resolve it with a comment, `gh issue close <n>`, and a pointer added
to the map's Decisions-so-far. The frontier is the map's open, unassigned children with no open blocker, in map order.
