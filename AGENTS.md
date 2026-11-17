# Agent Instructions

## General Rules

- Follow existing project architecture and conventions.
- Follow [CODE_STYLE.md](CODE_STYLE.md) for all C++ code. Key rules to check before writing any C++:
  - Header guards: `#ifndef`/`#define`/`#endif`, never `#pragma once`.
  - Rule of Five: declaring any of the five special members requires declaring all five.
  - Comments: only for non-obvious functionality; explain **why**, not **what**.
- Make only changes necessary to fulfill the task. Avoid unrelated refactoring.
- Update tests and documentation when required by the changes.
- If requirements are ambiguous, ask before guessing.

## Testing and Validation

- After behavior changes, run the relevant checks defined in [CONTRIBUTING.md](CONTRIBUTING.md),
  using the CMake presets documented in [README.md](README.md).

## Git Operations

- Follow [CONTRIBUTING.md](CONTRIBUTING.md) for commit and pull request conventions.
- Create commits, push changes, or open pull requests only when explicitly asked.
