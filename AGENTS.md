# Notes for coding agents

## Skills to load for each kind of work

Agents do not pick skills on their own. Every task brief names the skills its
work needs, as a line "Load these skills first: …", and the agent loads them
before its first edit. The `qt-development-skills` ones come from the Qt
plugin for Claude Code; `dataviz` is a separate skill.

| Skill | What it is for here | When |
|---|---|---|
| `qt-development-skills:qt-ui-design` | Designing and auditing windows: hierarchy, spacing, alignment, density, empty states, focus and selection | Every design and every design review. The spec says which of its checks drove which decision |
| `dataviz` | Charts: curve colours, axes, legends, readouts, readability | Every design of the Plot's or the Trend's look or interaction |
| `qt-development-skills:qt-qml` | QML practice: bindings, layouts, delegates, loaders | Every task that writes or changes QML, before the first QML edit |
| `qt-development-skills:qt-qml-profiler` | Finding where QML is slow | When something lags, such as the Trend while recording |
| `qt-development-skills:qt-cmake-project` | CMake for Qt targets and QML modules | When a task touches CMake |
| `qt-development-skills:qt-qml-test` | Writing Qt Quick tests | When a task needs new QML tests |
| `qt-development-skills:qt-qml-review`, `qt-development-skills:qt-cpp-review` | Review passes; each starts six subagents, so they are costly | Only as an occasional quality pass the owner asks for, never in a routine task |

Why: on 17 November 2026 the owner found windows that looked unstyled. The
designers had been told only that they *may* use `qt-ui-design`, and most of
the other skills had not been used at all.
