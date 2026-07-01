---
name: engine-research
description: Read-only VertexForge engine research workflow for architecture mapping, call tracing, subsystem investigation, and planning support. Use when planning an engine change, investigating how a subsystem works, scoping a feature, answering "how does X work" or "where does Y live", or combining codebase exploration with external Vulkan/rendering/game-engine research.
---

# Engine Research

## Overview

Produce a verified evidence base before design or implementation: architecture map, exact file/line citations, representative call trace, relevant external research, constraints, and open questions. This skill is read-only unless the user explicitly asks for a plan artifact.

## Workflow

1. Frame the question in one sentence. Split it into no more than three codebase areas plus any external/theory questions that matter.
2. Read the most relevant repo context first: `CLAUDE.md` if present, nearby docs, existing tests, and `git log --oneline -- <path>` for changed areas.
3. Use `references/investigation-playbook.md` to choose entry points. Prefer `rg` for symbol and file discovery, then read exact files directly before making claims.
4. Trace the representative path end to end across VertexForge layers, usually `Editor -> Services -> Provider/Adapter -> Controller -> Core/Graphics`.
5. Use `references/external-research.md` only when the question depends on external API semantics, rendering theory, or library behavior. Prefer official docs/specs and cite URLs.
6. Resolve contradictions by re-reading the cited source file. Do not ship claims from memory, stale notes, or search snippets.
7. Stop at findings unless the user explicitly asks for implementation. Hand off a plan-ready evidence summary.

## Output Template

Use this shape unless the user asks for a different format:

```text
Question: one line.
Architecture: modules/layers involved and how they connect.
Key files: path:line list of load-bearing files and symbols.
Call trace: representative flow from entry point to implementation.
External: sourced API/theory notes with URLs, if applicable.
Constraints: ABI/DLL boundaries, singleton rules, shader/resource copy requirements, validation concerns, and compatibility limits.
Open questions: what remains unverified and why.
```

## Rules

- Cite every codebase claim with `path:line`.
- Treat memory files, prior notes, and generated summaries as leads only. Re-verify current files and symbols.
- Keep research focused on load-bearing files and the representative path. Do not enumerate every file in a subsystem unless enumeration is the task.
- Surface existing utilities, abstractions, and project patterns to reuse.
- Do not edit files, run builds, or run destructive commands while using this skill unless the user explicitly changes the task from research to implementation or verification.
- Do not state time-sensitive external facts without a current source.

## References

- `references/investigation-playbook.md`: VertexForge subsystem entry points, layer flow, and focused search prompts.
- `references/external-research.md`: when and how to use external Vulkan/library/rendering research, including source preferences and mapping findings back to the codebase.
