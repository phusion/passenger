This is the codebase for the Phusion Passenger application server. Mostly C++14 and Ruby.

Before starting work:

- read the table of contents in `./doc/DevHandbook.md` to discover available developer documentation. Read relevant documents as needed for the task.
- read `./doc/BasicArchitectureOverview.md` to learn foundational knowledge that aids you in determining research directions.

## Coding guidelines

- Make the smallest changes to existing interfaces as possible. Be averse to rewriting things.
- Prefer boring, explicit code over cleverness or premature abstraction. Keep the main code path easy to follow and centered on business logic; move incidental technical or secondary details into helpers when they obscure that flow. Some duplication is fine. Extract shared abstractions only when they clearly improve readability or eliminate substantial duplication, and avoid speculative generalization.
- Proper error handling
  - Shell scripts: use pipefail
  - When ignoring errors, only ignore specific errors, not blanket ignore all errors
- Commenting strategy:
  - Comment non-obvious context the code cannot express clearly: purpose, domain terms, responsibilities, input and output semantics, algorithm stages, invariants, caveats, and decisions. Explain complicated algorithms in high-level manner to aid human readability. Briefly state non-obvious class, module or method responsibilities. Put the comment where that information applies.
  - Write for a capable contributor new to the subsystem or platform. Use natural, plain English and precise technical terms where useful. Define unfamiliar concepts where introduced, explain how they relate to nearby code, and do not make readers derive their meaning from mechanics or call sites.
  - State purpose or constraints before mechanics. Keep comments short and local, put broader or cross-cutting rationale/caveats in the developer handbook, and do not narrate straightforward code.
- Before finishing a non-trivial change, verify it once: re-read the request, inspect the full diff, run relevant tests/checks, and look for missed requirements, wrong assumptions, guideline violations, edge cases, regressions, or unnecessary changes. Fix any concrete issues you find. Do not assume something must be changed; preserve correct code and avoid revisions made only for the sake of revising.

### For C++

- Prefer using internal utility library.
  - Prefer oxt/system_calls.hpp wrappers (e.g. oxt::open) over direct syscalls. If no wrapper available, loop until no EINTR.
  - Consult Utils.h (general utils), IOUtils.h (I/O), FileTools/*.h (file operations).
  - Prefer FileDescriptor or safelyClose() over close().
- Mind security: see SecureTempFileHandling.md; prefer safeReadFile() for reading files.

## Testability (not for shell scripts, Ansible)

- Keep core logic independently unit testable; isolate side effects and external interactions where practical.
- Add tests for significant behavioral changes and bug fixes. Prefer red/green testing.

## Documentation style

- Use natural, plain English. Use sentence case for headings. Do not cap line widths.
- Write for a capable developer who is new to this codebase. Explain purpose or constraints before implementation details. Introduce technical terms before using them densely. Prefer concrete subjects and actions. Use examples when they explain behavior more quickly than a list. Include only information that helps a contributor find the correct code, make a decision, or avoid a likely mistake. Leave exhaustive method behavior and minor edge cases to the code and tests. Do not repeat information that an example or an earlier section already makes clear. After drafting, remove every sentence that is merely true but not useful.
- Keep information that helps readers understand a non-obvious design, find where to make a change, make a decision, or avoid a mistake. Remove repetition and details easily recovered from code or tests.
Keep user documentation focused on public setup, behavior, and limitations. Omit internals and exhaustive behavior.

## Escalation policy

Optimize for the underlying goal, not literal compliance. Continuously check whether requests, requirements, contracts, constraints, and assumptions actually make sense. If you discover a medium/high-impact ambiguity, contradiction, bad assumption, or strategic/design problem, do not work around it or silently choose an interpretation: surface it and discuss it with me first. The later you discover the issue, the more important it is to reconsider the plan rather than defend work already done.

Use judgment to handle low-impact problems autonomously and mention noteworthy ones afterward.

When progress stalls or complexity grows unexpectedly, stop and broaden the search space instead of iterating mechanically. Reconsider the approach itself and proactively explore materially different strategies—such as simplifying or reframing the problem, improving reproduction or observability, using existing tools/libraries, or changing assumptions or constraints.

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
