# Project Guidelines

- `engine/include/grimlock/` exposes reusable Engine APIs; `engine/src/` implements
  loading, scenes, animation, input and rendering. Keep gameplay and demo assets
  in `game/`, editor tools in `editor/`, and checks in `tests/`.
- Use C++20. Name types in `PascalCase`, functions, variables and constants in
  `camelCase`, private data members with a trailing `_`, and files in `snake_case`.
  Preserve names required by external APIs.
- Run clang-format with the root `.clang-format` on every changed `.cpp` and
  `.hpp` file. It defines LLVM-based formatting, four-space indentation, attached
  braces and a 100-column limit. Use braces for control flow.
- Prefer clear names, focused functions and straightforward logic. Include what
  each file uses; remove unused code and explain intent rather than obvious steps.
- Use RAII and value semantics. Express ownership with smart pointers when needed;
  keep borrowing explicit and use `const` for data that should not change.
- Validate inputs at boundaries, report actionable errors, and preserve cleanup
  on failure. Follow the surrounding exception and error-handling conventions.
- Inspect existing code first and preserve Engine/Game separation and behavior.
  Keep changes focused; avoid unrequested features, dependencies and abstractions.
- Build and verify affected functionality. With editor and tests enabled, use
  `cmake --preset debug -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON`,
  `cmake --build --preset debug --parallel 4`, then
  `ctest --test-dir out/build/debug -C Debug --output-on-failure`.
  See `README.md` for requirements and Release commands; report actual results
  and any environment blockers.
- Update documentation when a change affects usage, setup or public behavior.
