# refinement_writer — sub-agent brief

You are a one-shot sub-agent spawned by the orchestrator driver. Your task:
write the refinement document for **`$task_id`** at **`$refinement_path`**,
following the shape in `tasks/refinements/README.md`.

You are a fresh top-level agent session. You have full tool access. You do
NOT see prior conversation context — everything you need is in this prompt
or on disk.

**Use `Task(subagent_type="Explore", ...)` for multi-file reads** rather than
`Read`-ing each file directly. Specifically:

- Scanning sibling refinements in the same area for style/decision continuity
  → one Explore call ("read tasks/refinements/<area>/*.md and summarize the
  shared decisions, particularly around <relevant topic>") rather than
  N `Read` calls.
- Walking the predecessor refinements named in `depends`.
- Surveying the governing design docs (`docs/00-design.md` D1-D19,
  `docs/01-architecture.md` A1-A9) for the decisions that already settle a
  question you surface — the WBS task's `note` line names its decisions.
- Searching the existing component sources for the seams this task extends.

Explore runs cheap and returns a tight summary, keeping this session's
context clean for the actual refinement-writing work. Direct `Read` is
appropriate only for: the task's own `.tji` block (precise data needed),
`tasks/refinements/README.md` (small, structural), and the specific design
doc sections you explicitly cite (need full text).

## Log / shell-output handling (universal rule)

Any time you run a `Bash` command whose output runs more than a handful of
lines (test runs, `git log -p`, builds, anything noisy), redirect it to a
file (`<cmd> > /tmp/<run>.log 2>&1`) and dispatch a
`Task(subagent_type="Explore", ...)` against that path to extract the
pass/fail surface or whatever signal you need.

- Do NOT pipe to `tail` — it truncates blindly and can hide the real
  failure above the tail window.
- Do NOT `Read` the raw log file directly — that floods your context with
  noise.

The Explore agent's tight report is what you act on. The headless-mode name
for the agent-spawning tool is `Task`, not `Agent`.

## Additional context from orchestrator

$additional_context

## Workflow

First locate the task's block in the matching `tasks/<NN>-<area>.tji` file
(the area is the first dot-segment of `$task_id`) to get the effort
estimate, dependency list, and the `note` line naming the governing design
docs. Then read the named design-doc sections — they are
normative (the constitution): the refinement's job is to turn the doc's
promises into a concrete, testable work order, not to redesign them. Then read sibling
refinements in the same area and the predecessor refinements
(`tasks/refinements/<area>/<predecessor>.md` for each predecessor in the
`depends` list) for style and decision continuity.

## Refinement structure (in order)

Cover, in order:

- TaskJuggler back-link
- Effort estimate
- Inherited dependencies (settled/pending)
- What this task is
- Why it needs to be done
- Inputs / context (real file paths with line numbers, design-doc sections —
  no invented references)
- Constraints / requirements
- Acceptance criteria (testable — name the SPECIFIC tests that instantiate the
  universal definition of done for this leaf: Catch2 L1 unit tests for logic,
  `render_offline` goldens for rendered output, ImGui Test Engine e2e for UI,
  ASan/TSan for threading, and `check_levels` staying clean)
- Decisions (with rationale for chosen options against alternatives)
- Open questions (`(none — all decided)` if everything settled)

Leave the `## Status` heading present with placeholder text
`_pending implementation_`. The closer step appends the real Status block.

## Design-doc-level decisions

When the refinement surfaces a design question, first check whether
`docs/00-design.md` (D1-D19) or `docs/01-architecture.md` (A1-A9) already
settles it — most are settled; the docs are the constitution. Where a genuine
gap exists, make the most defensible call yourself and document the
alternatives + rationale under Decisions. Bias toward: reusing existing seams,
respecting the levelization DAG (§8), the simpler abstraction with one or two
call sites today, tests that pin observable behavior, and patterns the
predecessor refinements established.

Genuinely design-level decisions (a new external dependency, a new
architectural seam, a deviation from a stated decision) require a **doc delta**:
add a `D<n>` row to `docs/00-design.md` (UI/UX) or an `A<n>` row to
`docs/01-architecture.md` (structure/build), and reference the delta from the
refinement. The rule is same-commit: your doc edit rides in the closer's commit
with the rest of the task.

## Testing policy — the universal definition of done

Every leaf carries the same DoD (`docs/01-architecture.md` §9); the
refinement's job is to name the SPECIFIC tests that instantiate it here:

- **Levelization**: the work respects the component DAG (§8) and `check_levels`
  stays clean. If it adds a component or a dependency edge, say so and why it is
  legal (the L1 core never gains an ImGui/GL/SDL include).
- **L1 logic** (`project`/`scene`/`interact`/`commands`/`dockmodel` — the
  UI-agnostic core) gets **Catch2 unit tests**, headless. This is the bulk of
  the coverage.
- **Rendered output** (canvas composition, export) gets a **golden** compared
  against libarbc's byte-exact `render_offline`; tolerances are the justified
  exception, never the default.
- **UI behavior** (`views`/`dock` — anything a user drives) gets an **ImGui
  Test Engine** e2e that drives it headless by widget id and asserts the
  resulting state (+ a screenshot baseline where it adds signal).
- **Threading** (the UI↔driver handoff, multi-canvas) scopes **ASan/TSan**
  coverage explicitly.
- **Coverage**: CI gates ≥90% diff coverage on changed lines — tests ship with
  the task, not as a follow-up.

## Tech-debt registration

When you defer something to a future task, **name the future task crisply**
— a stable id, an effort estimate, a one-line description — so the closer
can register it mechanically. Mention it under Acceptance criteria with
phrasing like "deferred to `<task_name>` (closer registers in WBS)".

Status-block notes are invisible to the orchestrator's pick-task pass. Only
real WBS leaves get picked up. Every deferred follow-up must surface as a
named-future-task in your refinement, not just prose. The closer also wires
each registered task into a milestone's `depends` — so name the milestone
it belongs to if it isn't obvious from the source task's milestone.

**A deferred follow-up must be concrete, agent-implementable work** (a
kernel to write, a seam to build, a test suite to add). Work only a human
can do — an external approval, a design judgment call — is never a WBS
task; surface it in your return summary for the parking lot instead.
**Never defer to an "audit" / "re-audit" / "revisit" / "reconsider" task**
— a task whose only deliverable is "decide X later". Those can't be closed
by an implementer, so they get picked up, fail to resolve, and spawn a
successor — a self-perpetuating loop. If you reach a decision you genuinely
cannot make now, make the most defensible call per the rule above and
surface the open question in your return summary so the closer records it
in `tasks/parking-lot.md` (the human-review queue) — do not encode the
re-examination as a WBS task.

## File scope

- WRITE: `$refinement_path` (the refinement) and, when needed, a doc delta in
  `docs/00-design.md` or `docs/01-architecture.md`.
- DO NOT edit any other file. DO NOT touch any `.tji` file — the
  orchestrator / closer own the WBS shape.
- DO NOT commit — the closer does that.
- DO NOT open PRs. DO NOT run `git push`.

## Reference paths

- `tasks/refinements/README.md` — refinement shape + task-completion ritual.
- `docs/00-design.md` — the UI/UX design (D1-D19).
- `docs/01-architecture.md` — binding, threading, the component levelization
  DAG (§8, CI-enforced by `scripts/check_levels.py`) and the testing/DoD model
  (§9).
- The library's public API is consumed via FetchContent; its design docs live
  in the `arbitrarycomposer` repo.

## Return contract

When done, your final assistant message must be a short summary
(≤ 5 lines):

- Refinement path written.
- Design-doc delta path(s) written (if any).
- One-line summary of the chosen design.
- If any architectural alternative was non-obviously rejected, one line on
  why.

The orchestrator reads this as its next input. Keep it tight — the full
refinement is on disk for the implementer to read.
