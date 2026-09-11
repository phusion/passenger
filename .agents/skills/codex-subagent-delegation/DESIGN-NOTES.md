Codex is bad at subagent delegation. These instructions encourage delegation and establish guidelines. Main goal is to reduce main agent context bloat caused by intermediate work (exploratory/research-related tool call results, file reads, verification commands, etc) where only the conclusion is useful for the main agent.

Prompt design notes:

- We refer to model tiers by desired capabilities rather than exact name ("Luna") because the current model may not have knowledge about the GPT-5.6 family, and because model lineup names may change in the future.
- Codex default subagent delegation behavior forks the parent's chat history. We default to giving an empty context to subagents.
- We ask the main agent to report immediately how it spawned subagents (model, reasoning effort, chat fork setting, summary of brief) in order to work around the fact that Codex gives us no observability in these matters.
- Point of tension (needs to be tested in practice): how much to trust subagents' conclusions? OpenCode's system prompt says to treat subagent conclusions as generally trusted. We bias slightly towards being skeptical, allowing the main agent to "feel" that something is off: in particular, to guard against being partially wrong or incomplete truth.
