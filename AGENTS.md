# Project Guidelines

- Preserve Engine/Game/Editor boundaries: reusable APIs and systems in `engine/`,
  gameplay and assets in `game/`, editing tools in `editor/`, checks in `tests/`.
- Use C++20, `PascalCase` types, `camelCase` functions/variables/constants,
  trailing `_` on private data members, and `snake_case` filenames.
- Run `clang-format -i --style=file` on every modified project-owned `.cpp`,
  `.hpp`, `.h`, and `.cc` file. Follow the root `.clang-format`: four spaces,
  no tabs, attached braces, consistent pointers/references, and 120 columns.
- Keep code readable: separate logical steps with blank lines, use focused
  functions and clear names, and avoid compressed statements and needless nesting.
  Explain intent in comments; omit obvious narration.
- Use RAII, explicit ownership, and `const` where appropriate. Validate boundary
  inputs and follow existing error handling without hiding failures.
- Inspect surrounding code and preserve behavior. Keep changes focused; avoid
  unrequested features, dependencies, abstractions, and architectural refactoring.
- Build and test affected functionality using the CMake presets and CTest commands
  in `README.md`. Report actual results and blockers. Update docs when relevant.
