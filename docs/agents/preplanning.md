# Preplanning

The analysis that comes before planning (`docs/GROUND_UP_WORKFLOW_DESIGN.md` "Planning"). `/eaw-to-spec` and
`/eaw-to-tickets` run it before they plan. A routine fix with a clear brief skips planning, and this with it.

## Reuse a current record

Read the issue and its comments. If a comment starting `## Preplanning` exists and nothing it relies on has changed
since (the code it names, the decisions it cites), use it and go on to planning. Otherwise do the analysis.

## Do the analysis

1. Read the domain docs first (`docs/agents/domain.md` "Before exploring, read these").
2. Find the code, tests, fixtures and decisions in the area. Check every assumption the plan will rest on against the
   code, and note where you checked it. An unchecked assumption is an open question, not a finding.
3. Name what the change touches: public interfaces, simulation and determinism contracts (`docs/SIMULATION.md`), the
   save format, mod compatibility (`docs/MODS.md`), build gates and the release procedure.
4. Find the existing test seams: a CTest entry, a headless scenario or a fixture.
5. Settle what the repo can answer. A focused reproduction, `/eaw-research` or `/eaw-prototype` can settle an
   uncertainty the code cannot.

When the change touches a shared API, a simulation contract, the save format or a build gate, also run the analysis in
a fresh read-only subagent. Give it the issue and these steps, not your conclusions, so it does not just confirm what
was discussed, and reconcile its findings with yours.

Depth follows risk: a small change can have a record of a few lines.

## Record it

Post the record as a comment (`docs/agents/issue-tracker.md` "Conventions"). It goes on the issue the work comes from;
when there is no issue yet, post it on the first issue the skill publishes (the spec, or the first ticket).

```markdown
## Preplanning

**Affected:** the modules and contracts the change touches.
**Checked:** each assumption the plan rests on, and the file, test or command that confirmed it.
**Test seams:** the existing CTest entries, headless scenarios or fixtures to verify through, or "none yet".
**Open decisions:** each question only the maintainer can settle, or "None".
**Blockers:** anything that must happen before the first slice can start, or "None".
```

## Stop on open decisions

When **Open decisions** is not "None", do not plan around them. Bring them to the maintainer, with `/eaw-grill-with-docs`
or, when they span several sessions, `/eaw-wayfinder`. Plan once they are settled, and update the record. A durable,
consequential trade-off becomes a `docs/DECISIONS.md` section (`docs/agents/domain.md` "Record decisions in
DECISIONS.md").
