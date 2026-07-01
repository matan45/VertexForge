---
name: make-no-mistakes
description: Extra precision and self-verification directive for error-sensitive work. Use when the user invokes "make no mistakes", asks for maximum rigor, requests a high-stakes review or refactor, or needs source-backed facts, careful validation, and explicit uncertainty instead of guesses.
---

# Make No Mistakes

## Overview

Apply a higher bar for confidence before acting or answering. Verify claims against available sources, preserve behavior unless change is explicit, and state uncertainty when something cannot be confirmed.

## Operating Rules

- Verify before relying on memory. Read the actual file, symbol, signature, value, log, schema, or source when it is checkable.
- Trace logic step by step before editing. For code, reason through control flow, lifetime, errors, and concurrency implications.
- Preserve existing behavior during refactors unless the user explicitly asks for a behavior change. Call out intentional deviations.
- Prefer narrow, source-backed claims over broad assertions. Cite exact files, lines, commands, or URLs when precision matters.
- If a fact is not confirmed, say what is known, what is inferred, and what remains uncertain.
- Before destructive, irreversible, or outward-facing actions, re-read the target and confirm if the request or target is ambiguous.

## Verification Standard

- Run the smallest meaningful verification for the risk involved: tests, builds, linters, static checks, reproduction steps, or direct source inspection.
- For numeric or logical answers, recompute independently enough to catch arithmetic or assumption errors.
- For modern or time-sensitive facts, verify from current authoritative sources.
- For code reviews, prioritize concrete defects, regressions, missing tests, and safety risks over style preferences.

## Scope

This skill applies to the current task after invocation. It does not replace project instructions, user instructions, or tool safety rules.
