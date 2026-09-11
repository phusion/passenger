---
name: codex-subagent-delegation
description: Apply iff running under OpenAI Codex harness; governs subagent delegation to reduce main-agent context use.
---

# Subagent delegation (applicable only to OpenAI Codex harness)

Use subagents proactively when delegation would materially preserve main agent's context, parallelize independent work, or provide useful independent judgment. Do not wait for user to explicitly request subagents.

In particular:

- Delegate self-contained work likely to generate substantial disposable intermediate context — like reasoning, searches, tool output, or exploratory dead ends—when main agent primarily needs conclusions. Good candidates include research, codebase exploration, large-file or log analysis, test investigation, documentation lookup, inventories, comparisons, and independent review.
- Parallelize independent workstreams when useful. Do not parallelize tightly coupled code edits unless their scopes are clearly disjoint.
- Do not delegate trivial work, or work that requires most of the main conversation's evolving reasoning and context. Delegation should reduce rather than duplicate context and coordination.
- Give each subagent a self-contained brief containing all task-relevant context: objective, relevant constraints/context, scope, and expected output.
  - Avoid unrelated parent context; minimize inherited parent history, default to `fork_turns="none"`.
  - If parent conversation history is materially useful, fork only the smallest number of recent turns needed. Use `fork_turns="all"` only when the subtask genuinely depends on most or all parent history.
- Ask subagents to return concise conclusions, supporting evidence or references, important uncertainty, and anything they suspect remains unchecked. Keep raw searches, logs, and other bulky intermediate output in the subagent thread.

Choose subagent model tier according to judgment required, not merely workload or technical depth of the work:

- Use fast, efficient workhorse tier that remains capable of substantial technical and agentic work, like Luna, for bounded, well-specified work where search space and success criteria are reasonably clear.
- Use balanced, stronger-judgment tier like Terra when the work requires materially more judgment about relevance, completeness, relationships, competing explanations, or what to investigate next.
- Use advanced/flagship tier like Sol (or stronger) when task is ambiguous, consequential, adversarial, open-ended or when success depends on unusually strong judgment, synthesis, or recognition of unexpected findings.
- Match reasoning effort to task difficulty and required reliability.
  - Do not use xhigh/max effort merely to compensate for a tier whose judgment is insufficient for the task; escalate tier instead.
  - For Luna-tier models, prefer high for substantive delegated work; use medium only for simple, low-risk, highly mechanical work with clear success criteria or easy verification; do not use lower effort.
- Only select GPT-5.6-family or newer models.

Immediately after spawning a subagent, briefly report its task/purpose, `fork_turns`, requested model/reasoning effort (or inheritance), and a concise summary of the brief sent to it.

Main agent remains responsible for the result. Treat subagent output as evidence, not ground truth: it may be incomplete, partially correct, or mistaken. Check it for plausibility, completeness, contradictions, and missed avenues before relying on it. If result seems weak, incomplete, surprising, or inconsistent with other evidence, send the subagent targeted follow-up instructions or use another subagent for independent verification. Prefer continuing an existing subagent investigation when retaining its accumulated context is useful rather than importing its intermediate work into the main context.

When a later step depends on a subagent's result, wait for it. Otherwise, continue independent main-agent work while subagent runs. Synthesize only information needed for the main task back into the main thread.
