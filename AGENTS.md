This is the codebase for the Phusion Passenger application server. Mostly C++14 and Ruby.

Before starting work:

- read the table of contents in `./doc/DevHandbook.md` to discover available developer documentation. Read relevant documents as needed for the task.
- read `./doc/BasicArchitectureOverview.md` to learn foundational knowledge that aids you in determining research directions.

## Coding guidelines

- Prefer boring, explicit code over cleverness or premature abstraction. Keep the main code path easy to follow and centered on business logic; move incidental technical or secondary details into helpers when they obscure that flow. Some duplication is fine. Extract shared abstractions only when they clearly improve readability or eliminate substantial duplication, and avoid speculative generalization.
- Surgical changes:
  - Keep changes tightly scoped to the requested outcome. Avoid unrelated cleanup, refactoring, formatting, or stylistic changes.
  - Make low-impact refactorings autonomously when needed for a clean implementation or readability, and remove code made obsolete by the change. Follow existing project conventions unless there is a good reason not to.
  - Leave unrelated pre-existing issues unchanged. Report material ones without blocking the requested work.
- Proper error handling
  - Shell scripts: use pipefail
  - When ignoring errors, only ignore specific errors, not blanket ignore all errors
- Commenting strategy:
  - Comment non-obvious context the code cannot express clearly: purpose, domain terms, responsibilities, input and output semantics, algorithm stages, invariants, caveats, and decisions. Explain complicated algorithms in high-level manner to aid human readability. Briefly state non-obvious class, module or method responsibilities. Put the comment where that information applies.
  - Write for a capable contributor new to the subsystem or platform. Use natural, plain English and precise technical terms where useful. Define unfamiliar concepts where introduced, explain how they relate to nearby code, and do not make readers derive their meaning from mechanics or call sites.
  - State purpose or constraints before mechanics. Keep comments short and local, put broader or cross-cutting rationale/caveats in the developer handbook, and do not narrate straightforward code.
- Before finishing a non-trivial change, do one final verification pass: re-read the request, inspect the full diff, run appropriate tests/checks, and look for missed requirements, wrong assumptions, guideline violations, relevant edge cases, regressions, or unnecessary changes. Fix concrete issues you find and repeat affected checks when needed. Preserve correct code; do not revise merely for the sake of revising.

### For C++

- Prefer using internal utility library.
  - Prefer oxt/system_calls.hpp wrappers (e.g. oxt::open) over direct syscalls. If no wrapper available, loop until no EINTR.
  - Consult Utils.h (general utils), IOUtils.h (I/O), `FileTools/*.h` (file operations).
  - Prefer FileDescriptor or safelyClose() over close().
- Mind security: see SecureTempFileHandling.md; prefer safeReadFile() for reading files.

## Testing principles (not for shell scripts, Ansible)

- Scale testing to behavioral complexity, regression risk, and failure impact, not diff size. For bug fixes and changes to complex or order-sensitive behavior—such as concurrency/async ordering, state machines, retries/timeouts, protocols, persistence, or security-sensitive code—add the smallest focused regression coverage that meaningfully proves the behavior. If you omit such coverage, state the concrete reason.
- Prefer deterministic tests. For async or concurrent behavior, control scheduling, clocks, I/O, and failure injection where practical rather than relying on sleeps or wall-clock timing. Avoid tests that merely mirror the implementation or duplicate existing coverage. Prefer red/green testing for behavioral changes when practical, especially for bug fixes.
- Keep core logic independently testable where practical, and isolate side effects or external interactions when useful. Small, low-risk refactorings to improve testability are fine. If useful coverage would require a broad or intrusive refactor or architectural change, do not expand the current change silently; report the limitation and propose it as follow-up work. Continue the requested work unless the inability to test leaves material uncertainty about correctness, in which case apply the escalation policy.

## Documentation principles

- Use natural, direct, plain English. Prefer concrete subjects and actions. Avoid canned introductions and inflated claims. Use sentence case for headings. Do not cap line widths.
- For internal developer documentation:
  - Write for a capable developer who is new to this codebase. Explain purpose or constraints before implementation details. Introduce technical terms before using them densely. Use examples when they explain behavior more quickly than explanation alone.
  - Keep information that helps readers understand a non-obvious design, find where to make a change, make a decision, or avoid a mistake. Leave implementation details, exhaustive behavior and minor edge cases to the code and tests when they are easily recovered there.
- Keep user documentation focused on public setup, behavior, and limitations. Omit internals and exhaustive behavior.
- Organize content around distinct information readers need, not a repeated template. Add a section only when it contains substantial, distinct information. Avoid routinely giving every topic matching sections such as "What it is"/"Why it matters"/"How it works". Integrate short explanations of purpose or rationale into the relevant paragraph.
- Use headings to help readers navigate distinct topics without fragmenting closely related material. Use conclusions to synthesize long or complex documents, not merely repeat earlier content.
- Before finalizing, remove anything that does not materially aid understanding. Avoid repeating information that an example or an earlier section already makes clear.

## Version control commit and pull request messages

Write version control commit messages and PR descriptions for human reviewers. Apply the documentation principles, but optimize specifically for reducing the mental effort needed to understand and review the diff.

Describe changes at the level of intent, behavior, and design rather than code mechanics. Ground inferred intent and design in the request, conversation, tests, relevant repository history, and contrast with previous behavior. Do not invent unsupported motivation or put unresolved uncertainty about intent into the message. When intent is materially unclear, inspect related history; if focused research does not resolve an ambiguity that could materially change the message, ask focused questions. Otherwise, write the best grounded message available.

Scale the explanation to the review burden. For large or conceptually broad diffs, start with enough high-level context to orient the reviewer: what the change does, how the main parts fit together, and what to expect. This is useful even when it could eventually be inferred from the diff. For small, self-explanatory diffs, avoid a redundant summary unless there is useful non-obvious context.

Include non-obvious information that materially helps review, such as the prior problem or motivation, design decisions and rationale, research findings or constraints, tradeoffs, caveats, consequences, meaningful validation or rollout information, and areas deserving particular attention. Do not treat this as a checklist or invent significance to fill it.

Do not narrate the diff, list files or symbols, enumerate obvious edits, or add generic claims and filler. Mention implementation details only when needed to explain a design choice or tradeoff.

For commits, be concise. Use a clear subject describing the actual change or outcome rather than a vague label such as "Fix X". Add a short body only when context or rationale materially aids understanding.

For PRs, provide enough orientation and context to make review easier, but no more. Avoid rigid templates and unnecessary sections.

## Escalation policy

Optimize for the underlying goal, not literal compliance. As you work, sanity-check whether requests, requirements, contracts, constraints, and assumptions actually make sense. If you discover a medium/high-impact ambiguity, contradiction, bad assumption, or strategic/design problem, stop and investigate it rather than working around it or silently choosing an interpretation. Make the issue concrete, then surface it and discuss the material decision with me before proceeding with that decision. The later you discover the issue, the more important it is to reconsider the plan rather than defend work already done.

Use judgment to handle low-impact problems autonomously and mention noteworthy ones afterward.

When progress stalls or complexity grows unexpectedly, stop iterating on the current approach and broaden the search space instead of iterating mechanically. Reconsider the approach itself and proactively explore materially different strategies—such as simplifying or reframing the problem, improving reproduction or observability, using existing tools/libraries, or changing assumptions or constraints.

## Developer handbook

The handbook is a series of Markdown files in `doc/`. Purposes of the handbook:

- Teaches a capable human or AI developer, who does not know this codebase, how this codebase works so that they can contribute effectively. For AI, the handbook functions as a series of skills, with an index functioning as a skill router.
- Documents important design decisions, rationale and constraints so they don't get lost or become implicit.

Content coverage:

- Overall architecture and/or flow
- Important concepts and constraints
- Important design patterns where non-obvious
- Important or non-obvious design decisions
- Important subsystems

Writing guidelines (in addition to "Documentation style"):

- Use `doc/DevHandbook.md` as a concise, keyword-rich topic index and skill router.
- Give each major topic one canonical document and each document one primary topic.
  - A topic is major when changing it safely requires a distinct mental model because it has its own concepts, constraints, failure modes, platform behavior, or reasons to change. Code-module boundaries alone do not determine document boundaries.
- When writing an Architecture document, keep it as a map of components, main flows, and system-wide constraints. Summarize and link to canonical topic documents instead of putting subsystem details there.
- At topic boundaries, explain only the local interaction and link to the canonical document. Do not duplicate the complete policy.
- Document important rationale and non-obvious design decisions. Omit trivial information and content already covered by user documentation, AGENTS.md, or CONTRIBUTING.md.
- Must reflect current behavior rather than idealized goal. If they differ, document the divergence.

Update the handbook in the same change when architecture, flows, major concepts, constraints, patterns, decisions, or subsystems change.

## OpenAI Codex harness

If running under OpenAI Codex harness, read and follow these before starting work:

- `.agents/skills/codex-subagent-delegation/SKILL.md` — governs subagent delegation

## Rules for avoiding caveats

- When bypassing Rake:
  - Run tests with cwd `test/`
  - Manually set env vars defined in config.rb (if any). This file is normally loaded during Rake startup.
