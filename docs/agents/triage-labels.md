# Triage labels

The skills speak in terms of five canonical triage roles. This file maps them to the label strings used in this
repo's GitHub issues. Adapted from the `setup-matt-pocock-skills` seed (`mattpocock/skills`, MIT).

| Role | Label in this repo | Meaning |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | The maintainer needs to evaluate this issue |
| `needs-info` | `needs-info` | Waiting on the reporter for more information |
| `ready-for-agent` | `ready-for-agent` | Fully specified, ready for an agent |
| `ready-for-human` | `ready-for-human` | Needs human implementation or judgment |
| `wontfix` | `wontfix` | Will not be actioned |

Category roles use GitHub's `bug` and `enhancement` labels. Every triaged issue carries exactly one category and one
state label.

Each issue also gets one `roadmap:<box>` label, the box of the roadmap flowchart it belongs to (README.md "Roadmap"). An
issue with no box shows under "Not on the chart" on the roadmap page. Imported issues also carry `imported`.
