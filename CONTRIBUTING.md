# Contributing

## General

- Follow [CODE_STYLE.md](CODE_STYLE.md).
- Build successfully (see [README.md](README.md)).
- Run clang-format before submitting (`clang-format-check` preset).
- Add and run unit tests (`tests` preset).
- Run clang-tidy and fix reported issues (`clang-tidy-check` preset).
- Run coverage and meet >= 90% on `src/` (`coverage-report` preset).

## Commits

- One logical change per commit.
- Subject line format: `<area>: <summary>`
  - `<area>` is the component/module/subsystem/tool affected, in lowercase: `my_component`, `build`,
    `ci`, `docker`.
  - `<summary>` is in the imperative mood ("add", not "added" or "adds"),
    lowercase, with no trailing period. Avoid vague descriptions such as "update", "fix bug", or "changes".
- The subject line is at most 72 characters.
- Leave one blank line after the subject. Wrap the body at 72 characters.
- The body explains *why* the change is necessary, how it fixes the problem and, if relevant, the alternatives considered.
- Reference issues in the body: `Refs #123`.
- Example:
  ```text
  my_component: add json serialization method

  The application needs to save data as json.
  ```

## Pull requests

- Name branches `<area>/<short_description>`, for
  example `my_component/add_json_translation`.
- The title follows the commit subject format.
- The description includes:
  - **Why it was done**
  - **What was done**
  - **How it was tested**
  - Linked issues
- Rebase on `master` to resolve conflicts. Do not merge `master` into the branch.
