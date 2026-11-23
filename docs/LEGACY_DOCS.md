# Legacy documents

This repository was split out of an older checkout. Its design notes, reverse-engineering notes (`docs/re/`), plans,
audits and measurement logs stayed there. They are history and reference material, not maintained here: the rules
that still bind current code were rewritten into `docs/MODS.md`, `docs/SIMULATION.md` and `docs/DECISIONS.md`.

- Repository: <https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old>
- Pinned commit: `7f911b51f0d12d0d10e8218e726af3d7ace5d9c5` (on its `dev` branch). Every link below is a permalink to that commit.

## Reference convention

Cite a document that exists only in the old checkout as `legacy <path in the old repo>`, with the full path, never a
bare file name: `legacy docs/re/UI.md §9.2`, `legacy docs/PHASE4.md "BM - repeatable benchmark"`. The section after
the path uses the old document's numbering or heading. Search for `legacy docs/` to find every such citation.

Cite a document of this repository by its path, and a section of the three authoritative documents by its heading
in quotes: `docs/MODS.md "File resolution order"`. Their headings are not numbered.

`tools/lint/check_doc_refs.py` fails when a file cites a `docs/...md` that neither exists here nor is listed below,
cites a listed document without the `legacy` prefix, or cites a heading of the authoritative documents that does
not exist. To cite another old document, add its row to "Cited documents" (keep the table sorted).

## Superseded documents

These old documents have a current replacement. Cite the replacement; cite the old document (with `legacy`) only for
content that was not carried over.

| Old document | Replaced by | What moved |
|---|---|---|
| [docs/MODS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/MODS.md) | docs/MODS.md | current sections rewritten; the old one keeps the run logs and dated audits |
| [docs/MOD_COMPAT_DESIGN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/MOD_COMPAT_DESIGN.md) | docs/MODS.md | standing design rules; plan, risks and gap index stay in the old doc |
| [docs/DECISIONS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/DECISIONS.md) | docs/DECISIONS.md, docs/MODS.md, docs/SIMULATION.md | standing decisions split by topic; process and roadmap entries stay in the old doc |
| [docs/SIM_PARALLEL_DESIGN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/SIM_PARALLEL_DESIGN.md) | docs/SIMULATION.md | binding rules; plan, measurements and risks stay in the old doc |
| [docs/status/golden-checkpoints.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/status/golden-checkpoints.md) | docs/SIMULATION.md | CRC log format and comparison |
| [docs/BUILD.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/BUILD.md) | README.md, docs/SIMULATION.md | build and test commands in README.md; worker-count and services graph checks in docs/SIMULATION.md |
| [docs/PERFORMANCE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/PERFORMANCE.md) | docs/SIMULATION.md | "AI scheduling" and "Lua GC schedule"; measurements stay in the old doc |

## Cited documents

174 documents.

| Path |
|---|
| [docs/BUILD.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/BUILD.md) |
| [docs/DECISIONS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/DECISIONS.md) |
| [docs/DEFERRED_SIDE_GOALS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/DEFERRED_SIDE_GOALS.md) |
| [docs/MODS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/MODS.md) |
| [docs/MOD_COMPAT_DESIGN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/MOD_COMPAT_DESIGN.md) |
| [docs/OPT1_PLAN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/OPT1_PLAN.md) |
| [docs/PERFORMANCE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/PERFORMANCE.md) |
| [docs/PHASE4.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/PHASE4.md) |
| [docs/SIM_PARALLEL_DESIGN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/SIM_PARALLEL_DESIGN.md) |
| [docs/assets/CATALOG.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/assets/CATALOG.md) |
| [docs/assets/README.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/assets/README.md) |
| [docs/formats/SPEC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/formats/SPEC.md) |
| [docs/formats/validation.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/formats/validation.md) |
| [docs/re/A1_READINGS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A1_READINGS.md) |
| [docs/re/A2F_PRODUCTION_STALL_DIAG.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2F_PRODUCTION_STALL_DIAG.md) |
| [docs/re/A2F_RE_READS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2F_RE_READS.md) |
| [docs/re/A2F_T05_BUDGET_TRACE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2F_T05_BUDGET_TRACE.md) |
| [docs/re/A2GAL_READINGS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2GAL_READINGS.md) |
| [docs/re/A2G_T01_RESET_FLAG_PROBE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2G_T01_RESET_FLAG_PROBE.md) |
| [docs/re/A2H_GALACTIC_AI_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2H_GALACTIC_AI_RE.md) |
| [docs/re/A2H_T02_SPACE_TOTAL.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2H_T02_SPACE_TOTAL.md) |
| [docs/re/A2H_T06_TACTICAL_ETA.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2H_T06_TACTICAL_ETA.md) |
| [docs/re/A2I_T01_FRAME_ORDER.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2I_T01_FRAME_ORDER.md) |
| [docs/re/A2I_TOKENS_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2I_TOKENS_RE.md) |
| [docs/re/A2J_T02_PROBE_PLAN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2J_T02_PROBE_PLAN.md) |
| [docs/re/A2J_T03_INCOME.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2J_T03_INCOME.md) |
| [docs/re/A2J_T05_STORY_LOAD.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2J_T05_STORY_LOAD.md) |
| [docs/re/A2J_T07_LITERALS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2J_T07_LITERALS.md) |
| [docs/re/A2K_T01_BUILD_COST_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T01_BUILD_COST_RE.md) |
| [docs/re/A2K_T02_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T02_RE_ANSWERS.md) |
| [docs/re/A2K_T06_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T06_RE.md) |
| [docs/re/A2K_T07_STORY_LOAD_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T07_STORY_LOAD_RE.md) |
| [docs/re/A2K_T08_RE_QUESTIONS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T08_RE_QUESTIONS.md) |
| [docs/re/A2K_T10_CREDIT_NOTIFY_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2K_T10_CREDIT_NOTIFY_RE.md) |
| [docs/re/A2L_T03_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2L_T03_RE_ANSWERS.md) |
| [docs/re/A2M_R2_RE_A.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_R2_RE_A.md) |
| [docs/re/A2M_R2_RE_B.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_R2_RE_B.md) |
| [docs/re/A2M_R2_UW_BOUNTY_TRACE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_R2_UW_BOUNTY_TRACE.md) |
| [docs/re/A2M_R3_RE_A.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_R3_RE_A.md) |
| [docs/re/A2M_T04_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_T04_RE_ANSWERS.md) |
| [docs/re/A2M_T07_REBEL_SLICE_RUN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_T07_REBEL_SLICE_RUN.md) |
| [docs/re/A2M_T08_T10_T14_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/A2M_T08_T10_T14_RE.md) |
| [docs/re/ABILITIES_HARDPOINTS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/ABILITIES_HARDPOINTS.md) |
| [docs/re/ABILITY_PARTICLES.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/ABILITY_PARTICLES.md) |
| [docs/re/ABILITY_VISUALS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/ABILITY_VISUALS.md) |
| [docs/re/AI_GALACTIC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_GALACTIC.md) |
| [docs/re/AI_GALACTIC_2.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_GALACTIC_2.md) |
| [docs/re/AI_GALACTIC_3.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_GALACTIC_3.md) |
| [docs/re/AI_GALACTIC_TOKENS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_GALACTIC_TOKENS.md) |
| [docs/re/AI_TACTICAL.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_TACTICAL.md) |
| [docs/re/AI_TACTICAL_PERCEPTION.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AI_TACTICAL_PERCEPTION.md) |
| [docs/re/ANIM_PARTICLES.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/ANIM_PARTICLES.md) |
| [docs/re/AUDIO.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/AUDIO.md) |
| [docs/re/B0173_DEPLOYED_BEHAVIOR_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/B0173_DEPLOYED_BEHAVIOR_RE.md) |
| [docs/re/B0173_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/B0173_RE_ANSWERS.md) |
| [docs/re/BATTLE_TRANSITIONS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BATTLE_TRANSITIONS.md) |
| [docs/re/BKC1_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKC1_RE_ANSWERS.md) |
| [docs/re/BKG1_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKG1_RE_ANSWERS.md) |
| [docs/re/BKG3_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKG3_RE_ANSWERS.md) |
| [docs/re/BKR1_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKR1_RE_ANSWERS.md) |
| [docs/re/BKR2_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKR2_RE_ANSWERS.md) |
| [docs/re/BKT1_ATTACK_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKT1_ATTACK_RE.md) |
| [docs/re/BKX1_CADENCE_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKX1_CADENCE_RE.md) |
| [docs/re/BKX1_RENDER_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/BKX1_RENDER_RE.md) |
| [docs/re/COMMANDS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/COMMANDS.md) |
| [docs/re/CU8F_T01_T02_T07_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/CU8F_T01_T02_T07_RE.md) |
| [docs/re/CU8F_T04_T06_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/CU8F_T04_T06_RE.md) |
| [docs/re/CU8G_T02_ENV_PICK_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/CU8G_T02_ENV_PICK_RE.md) |
| [docs/re/CU8K_T02_T06_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/CU8K_T02_T06_RE.md) |
| [docs/re/CU8K_T03_T08_T09_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/CU8K_T03_T08_T09_RE.md) |
| [docs/re/DECORATIONS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/DECORATIONS.md) |
| [docs/re/FALLBACK_CAMPAIGNS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/FALLBACK_CAMPAIGNS.md) |
| [docs/re/FIGHTER_FLYING.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/FIGHTER_FLYING.md) |
| [docs/re/FIXPASS_GALACTIC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/FIXPASS_GALACTIC.md) |
| [docs/re/FOG_OF_WAR.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/FOG_OF_WAR.md) |
| [docs/re/GALAB_ABILITIES_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALAB_ABILITIES_RE.md) |
| [docs/re/GALACTIC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALACTIC.md) |
| [docs/re/GALACTIC_DIALOGS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALACTIC_DIALOGS.md) |
| [docs/re/GALACTIC_PLANET_MAP.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALACTIC_PLANET_MAP.md) |
| [docs/re/GALACTIC_REVEAL_CORRUPTION.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALACTIC_REVEAL_CORRUPTION.md) |
| [docs/re/GALACTIC_RNG_DRAWS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALACTIC_RNG_DRAWS.md) |
| [docs/re/GALAXY_MAP_PRESENTATION.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GALAXY_MAP_PRESENTATION.md) |
| [docs/re/GAME_SCORING.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/GAME_SCORING.md) |
| [docs/re/HARDPOINT_DISABLED.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/HARDPOINT_DISABLED.md) |
| [docs/re/HERO_CLASH.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/HERO_CLASH.md) |
| [docs/re/HOTSPOTS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/HOTSPOTS.md) |
| [docs/re/HUD_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/HUD_GAPS.md) |
| [docs/re/L1P4_LUA_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/L1P4_LUA_GAPS.md) |
| [docs/re/L1_S4_REMAINING.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/L1_S4_REMAINING.md) |
| [docs/re/LOB_SOLVER.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/LOB_SOLVER.md) |
| [docs/re/LUA_API.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/LUA_API.md) |
| [docs/re/LUA_SAVE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/LUA_SAVE.md) |
| [docs/re/LUA_SCRIPT_OUTPUT.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/LUA_SCRIPT_OUTPUT.md) |
| [docs/re/MAIN_LOOP.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MAIN_LOOP.md) |
| [docs/re/MAIN_MENU_BACKGROUND.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MAIN_MENU_BACKGROUND.md) |
| [docs/re/MAP1_RADAR_TERRAIN_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MAP1_RADAR_TERRAIN_RE.md) |
| [docs/re/MCX0_T07_UNLISTED_TAGS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX0_T07_UNLISTED_TAGS.md) |
| [docs/re/MCX1_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX1_RE_ANSWERS.md) |
| [docs/re/MCX2_T02_ASSET_SALVAGE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX2_T02_ASSET_SALVAGE.md) |
| [docs/re/MCX3_MEG_LOOKUP.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX3_MEG_LOOKUP.md) |
| [docs/re/MCX4_GAL2_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_GAL2_RE_ANSWERS.md) |
| [docs/re/MCX4_PRES_RE_ANSWERS_2.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_PRES_RE_ANSWERS_2.md) |
| [docs/re/MCX4_PRES_RE_ANSWERS_3.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_PRES_RE_ANSWERS_3.md) |
| [docs/re/MCX4_RE_ANSWERS_GALACTIC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_RE_ANSWERS_GALACTIC.md) |
| [docs/re/MCX4_RE_ANSWERS_TACTICAL.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_RE_ANSWERS_TACTICAL.md) |
| [docs/re/MCX4_RE_QUESTIONS_TACTICAL.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_RE_QUESTIONS_TACTICAL.md) |
| [docs/re/MCX4_T09_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T09_RE_ANSWERS.md) |
| [docs/re/MCX4_T10_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T10_RE_ANSWERS.md) |
| [docs/re/MCX4_T11_T12_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T11_T12_RE_ANSWERS.md) |
| [docs/re/MCX4_T13_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T13_RE_ANSWERS.md) |
| [docs/re/MCX4_T14_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T14_RE_ANSWERS.md) |
| [docs/re/MCX4_T15_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX4_T15_RE_ANSWERS.md) |
| [docs/re/MCX5_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX5_RE_ANSWERS.md) |
| [docs/re/MCX6_T05_CINEMATIC_BINDINGS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX6_T05_CINEMATIC_BINDINGS.md) |
| [docs/re/MCX8_BINDINGS_OBJECTS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX8_BINDINGS_OBJECTS.md) |
| [docs/re/MCX8_BINDINGS_PLAYERS_STORY.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MCX8_BINDINGS_PLAYERS_STORY.md) |
| [docs/re/MODC_LOADER.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MODC_LOADER.md) |
| [docs/re/MOV2_T05_TERRAIN_STOPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/MOV2_T05_TERRAIN_STOPS.md) |
| [docs/re/OBJECT_RUNTIME.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/OBJECT_RUNTIME.md) |
| [docs/re/OBJECT_SCRIPTS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/OBJECT_SCRIPTS.md) |
| [docs/re/OBJECT_TYPES.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/OBJECT_TYPES.md) |
| [docs/re/P1_CAMERA_INPUT.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/P1_CAMERA_INPUT.md) |
| [docs/re/P1_DOUBLE_CLICK_MOVE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/P1_DOUBLE_CLICK_MOVE.md) |
| [docs/re/P4_SIM_SYSTEMS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/P4_SIM_SYSTEMS.md) |
| [docs/re/PHASE2_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/PHASE2_ANSWERS.md) |
| [docs/re/PROJECTILE_NEBULA_DRAW.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/PROJECTILE_NEBULA_DRAW.md) |
| [docs/re/Q6_T15_RETAIL_RUN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/Q6_T15_RETAIL_RUN.md) |
| [docs/re/Q6_T27_ELIM_END_TIMING.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/Q6_T27_ELIM_END_TIMING.md) |
| [docs/re/Q6_T30_FIND_HINT_ORDER.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/Q6_T30_FIND_HINT_ORDER.md) |
| [docs/re/R1_T01_HARDPOINT_RUN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/R1_T01_HARDPOINT_RUN.md) |
| [docs/re/REMAINING_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/REMAINING_GAPS.md) |
| [docs/re/RENDER.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/RENDER.md) |
| [docs/re/S78N_T14_RUN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/S78N_T14_RUN.md) |
| [docs/re/S9_DEATH_CLONE_SHIELDS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/S9_DEATH_CLONE_SHIELDS.md) |
| [docs/re/SCRIPT_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SCRIPT_GAPS.md) |
| [docs/re/SKAI_T15_PLAN_RETEST.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SKAI_T15_PLAN_RETEST.md) |
| [docs/re/SKAI_T15_TARGET_LOCATION_FORCE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SKAI_T15_TARGET_LOCATION_FORCE.md) |
| [docs/re/SKAI_T17_SPACE_BUILD_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SKAI_T17_SPACE_BUILD_RE.md) |
| [docs/re/SKAI_T22_SPACE_AI_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SKAI_T22_SPACE_AI_RE.md) |
| [docs/re/SKIRMISH_SETUP.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SKIRMISH_SETUP.md) |
| [docs/re/SPACE_AND_OPEN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SPACE_AND_OPEN.md) |
| [docs/re/SPACE_AVOIDANCE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SPACE_AVOIDANCE.md) |
| [docs/re/STATION_HANGARS_SOUND.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STATION_HANGARS_SOUND.md) |
| [docs/re/STORY.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORY.md) |
| [docs/re/STORYB_T10_TRIGGER_AI.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORYB_T10_TRIGGER_AI.md) |
| [docs/re/STORYB_T16_SKY_DOME.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORYB_T16_SKY_DOME.md) |
| [docs/re/STORYB_T17_HIDDEN_UNITS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORYB_T17_HIDDEN_UNITS.md) |
| [docs/re/STORYB_T17_SPAWNS_AND_DEATH.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORYB_T17_SPAWNS_AND_DEATH.md) |
| [docs/re/STORY_PRESENTATION_LUA.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORY_PRESENTATION_LUA.md) |
| [docs/re/STORY_PRESENTATION_REWARDS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORY_PRESENTATION_REWARDS.md) |
| [docs/re/STORY_REWARDS_GALACTIC.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORY_REWARDS_GALACTIC.md) |
| [docs/re/STORY_T04_KESSEL_RUN.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STORY_T04_KESSEL_RUN.md) |
| [docs/re/STRATEGIC_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STRATEGIC_GAPS.md) |
| [docs/re/STY1_GENERIC_AUTOSAVE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STY1_GENERIC_AUTOSAVE.md) |
| [docs/re/STY1_OBJECT_SETTERS_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/STY1_OBJECT_SETTERS_RE_ANSWERS.md) |
| [docs/re/SUBSYSTEMS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/SUBSYSTEMS.md) |
| [docs/re/TACTICAL_COMBAT.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TACTICAL_COMBAT.md) |
| [docs/re/TACTICAL_GAPS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TACTICAL_GAPS.md) |
| [docs/re/TERRAIN_WATER_POST.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TERRAIN_WATER_POST.md) |
| [docs/re/TRN1_TERRAIN_GAMEPLAY.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TRN1_TERRAIN_GAMEPLAY.md) |
| [docs/re/TRU1_A_RE_ANSWERS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TRU1_A_RE_ANSWERS.md) |
| [docs/re/TRU1_C_RE.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TRU1_C_RE.md) |
| [docs/re/TRU1_GRAVITY_FIELD.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TRU1_GRAVITY_FIELD.md) |
| [docs/re/TSW_PLANET_DESTROY.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/TSW_PLANET_DESTROY.md) |
| [docs/re/UI.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/UI.md) |
| [docs/re/WEATHER_VISUALS.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/re/WEATHER_VISUALS.md) |
| [docs/shaders/CATALOG.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/shaders/CATALOG.md) |
| [docs/shaders/PORTING.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/shaders/PORTING.md) |
| [docs/status/apps-sim-split-plan.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/status/apps-sim-split-plan.md) |
| [docs/status/golden-checkpoints.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/status/golden-checkpoints.md) |
| [docs/status/modc-audit.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/status/modc-audit.md) |
| [docs/status/tag-ledger.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/docs/status/tag-ledger.md) |
| [tools/parity/README.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/parity/README.md) |
| [tools/probe_mod/g1_hops/README.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/g1_hops/README.md) |
| [tools/probe_mod/q2_traces/README.md](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/README.md) |

## Scripts

Scripts of the old checkout that comments or data here still name. None is called by the build or the tests:
the build and test entry point is `dev.cmd` (README.md), and `tools/test_suite.py` replaces `tools/lint/flaky_retry.py`
and `tools/lint/ctest_skips.py` (one retry for non-determinism tests, first failing output kept, skips reported by
suite with reasons). `build_agent.cmd`, `build_gate.cmd`, `build_module.cmd`, `build_fuzz.cmd`, `tools/regen_goldens.cmd`
and `tools/lint/test_tiers.py` have no counterpart here; the comments that named them now name the current command.

| Name in comments | Path in the old repo |
| --- | --- |
| `Play.cmd` | [tools/testbuild/launchers/Play.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/testbuild/launchers/Play.cmd) |
| `Play_Mod.cmd` | [tools/testbuild/launchers/Play_Mod.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/testbuild/launchers/Play_Mod.cmd) |
| `launcher.ps1` | [tools/testbuild/launchers/launcher.ps1](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/testbuild/launchers/launcher.ps1) |
| `package_testbuild.py` | [tools/testbuild/package_testbuild.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/testbuild/package_testbuild.py) |
| `run_trace.ps1` | [tools/probe_mod/q2_traces/run_trace.ps1](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_trace.ps1) |
| `run_galactic.cmd` | [tools/probe_mod/q2_traces/run_galactic.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_galactic.cmd) |
| `run_lua_probe.cmd` | [tools/probe_mod/q2_traces/run_lua_probe.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_lua_probe.cmd) |
| `run_skirmish.cmd` | [tools/probe_mod/q2_traces/run_skirmish.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_skirmish.cmd) |
| `run_story.cmd` | [tools/probe_mod/q2_traces/run_story.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_story.cmd) |
| `run_skai_t22_space.cmd` | [tools/probe_mod/q2_traces/run_skai_t22_space.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/q2_traces/run_skai_t22_space.cmd) |
| `run_capture.cmd` | [tools/probe_mod/parity_refs/run_capture.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/parity_refs/run_capture.cmd) |
| `analyze_hops.py` | [tools/probe_mod/g1_hops/analyze_hops.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/probe_mod/g1_hops/analyze_hops.py) |
| `a2g_port_vs_retail.py` | [tools/analysis/a2g_port_vs_retail.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/analysis/a2g_port_vs_retail.py) |
| `stress_land4.cmd` | [tools/benchmark/stress_land4.cmd](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/benchmark/stress_land4.cmd) |
| `behaviour_check.py` | [tools/goldens/behaviour_check.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/goldens/behaviour_check.py) |
| `fx2.py` | [tools/shaders/fx2.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/shaders/fx2.py) |
| `d3d9sm.py` | [tools/shaders/d3d9sm.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/shaders/d3d9sm.py) |
| `build_catalog.py` | [tools/shaders/build_catalog.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/shaders/build_catalog.py) |
| `check_cpp.py` | [tools/shaders/cpp/check_cpp.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/shaders/cpp/check_cpp.py) |
| `flaky_retry.py` | [tools/lint/flaky_retry.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/lint/flaky_retry.py) |
| `ctest_skips.py` | [tools/lint/ctest_skips.py](https://github.com/Gabriel-Barney/Empire-at-war-vulkan-old/blob/7f911b51f0d12d0d10e8218e726af3d7ace5d9c5/tools/lint/ctest_skips.py) |
