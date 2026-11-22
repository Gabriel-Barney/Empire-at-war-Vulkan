# Domain docs

How the engineering skills read this repo's domain documentation. Single-context layout. Adapted from the
`setup-matt-pocock-skills` seed (`mattpocock/skills`, MIT).

## Before exploring, read these

- **`GLOSSARY.md`** at the repo root: the project's terms. It is created when the first term is settled; if it does
  not exist yet, proceed without it.
- **`docs/DECISIONS.md`**: the standing decisions. They are this repo's ADRs (`docs/DECISIONS.md` "Decisions live in
  DECISIONS.md"); there is no `docs/adr/` folder. Read the sections that touch the area you work in.
- **`docs/SIMULATION.md`** for simulation, determinism and verification rules, and **`docs/MODS.md`** for mod loading
  and compatibility.
- `docs/LEGACY_DOCS.md` lists the older checkout's design and reverse-engineering notes. They are history and
  reference, cited as `legacy <path>`, not rules.

## Use the glossary's vocabulary

When your output names a domain concept (an issue title, a refactor proposal, a hypothesis, a test name), use the term
as `GLOSSARY.md` defines it. If the concept is missing, either you are inventing language the project does not use, or
there is a real gap to note for `/domain-modeling`.

## Record decisions in DECISIONS.md

A durable, consequential trade-off becomes a `## Title` section of `docs/DECISIONS.md`, in the format its introduction
gives (who decided and when, the rule, why). Only Gabriel adds, changes or removes a decision. Do not rename a heading
that code cites.

## Flag decision conflicts

If your output contradicts a section of `docs/DECISIONS.md`, `docs/SIMULATION.md` or `docs/MODS.md`, say so
explicitly, citing the heading, instead of silently overriding it.
