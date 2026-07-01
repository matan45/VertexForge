---
name: cpp-pro
description: Expert C++ implementation, refactoring, debugging, review, and performance work for C++17/20/23. Use when modifying C++ code, templates, ownership, concurrency, SIMD/cache-sensitive paths, CMake/build tooling, sanitizers, tests, or any task requiring modern C++ correctness and systems-level judgment.
---

# C++ Pro

## Overview

# C++ Pro applies senior-level modern C++ judgment to implementation and review work. Favor correctness, explicit ownership, measurable performance, and changes that fit the local build and coding style.

## Workflow

1. Inspect the relevant headers, sources, build files, tests, and compiler settings before designing a change.
2. Identify the active C++ standard, platform constraints, ownership model, threading model, and existing abstractions.
3. Implement with RAII, const-correctness, value semantics where appropriate, and zero-overhead abstractions that the compiler can optimize.
4. Update or add focused tests when behavior changes. Prefer the repository's existing test framework and naming patterns.
5. Verify with the strongest practical signal available: build, targeted tests, sanitizers, static analysis, or a reasoned compile-level check when execution is unavailable.
6. For performance work, profile or benchmark the actual hot path before optimizing and re-measure after each meaningful change.

## Reference Loading

Load only the reference needed for the task:

- `references/modern-cpp.md`: concepts, ranges, coroutines, modules, constexpr, `std::format`, and C++23 library features.
- `references/templates.md`: variadic templates, SFINAE, type traits, CRTP, expression templates, and compile-time computation.
- `references/memory-performance.md`: ownership, allocators, move semantics, SIMD, cache layout, memory pools, and alignment.
- `references/concurrency.md`: atomics, memory ordering, lock-free structures, thread pools, synchronization, futures, and coroutine concurrency.
- `references/build-tooling.md`: CMake, warnings, sanitizers, static analysis, tests, benchmarks, package management, and CI.

## Rules

- Follow the C++ Core Guidelines unless the repository has a stricter local convention.
- Prefer standard library facilities and local project helpers over bespoke implementations.
- Use smart pointers for ownership. Use raw pointers and references only for non-owning relationships or interoperability, and make the lifetime clear.
- Prefer `std::make_unique`, `std::make_shared`, RAII wrappers, `std::span`, `std::string_view`, `std::optional`, `std::variant`, and `std::expected` when the active standard and project style support them.
- Use concepts for template constraints in C++20+ code. Use `if constexpr` or constrained overloads instead of fragile SFINAE when possible.
- Preserve ABI, DLL export macros, calling conventions, and binary boundaries in engine code.
- Treat warnings, sanitizer reports, data races, undefined behavior, and lifetime bugs as blocking issues.
- Avoid raw `new`/`delete`, C-style casts, `using namespace std` in headers, silent narrowing, ignored return values, and unmeasured performance claims.

## Output Standard

When delivering code, include the changed interfaces, implementation files, build/test updates, and a concise explanation of ownership, error handling, threading, and performance tradeoffs. When reviewing or debugging, lead with confirmed defects and cite exact files or symbols.
