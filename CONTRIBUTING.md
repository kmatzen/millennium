# Contributing

Thanks for your interest in the Millennium Project. Here's how to contribute.

## Getting started

1. Fork the repository and clone your fork
2. Create a branch from `main` for your changes
3. Make your changes and ensure tests pass: `cd host && make test`
4. Push your branch and open a pull request

## Code style

- **C89** (`-std=c89`) for most host files — no mixed declarations and code
- **C99** (`-std=c99`) only where block-scoped declarations are necessary (simulator, jukebox)
- Compile with `-Wall -Wextra` and fix all warnings
- Avoid unnecessary comments — code should be self-documenting
- Use `snake_case` for functions and variables, `UPPER_CASE` for macros and constants

## Testing

All changes to the host software should pass existing tests:

```bash
cd host
make test
```

This builds and runs both unit tests and scenario tests. If you add new functionality, add corresponding tests:
- **Unit tests**: Add to `host/tests/unit_tests.c`
- **Scenario tests**: Add a `.scenario` file in `host/tests/`

## Pull requests

- Keep PRs focused on a single issue or feature
- Reference the issue number in the PR description (e.g., "Closes #12")
- Ensure `make test` passes before submitting
- Describe what changed and why in the PR body

## Reporting bugs

Open an issue with:
- What you expected to happen
- What actually happened
- Steps to reproduce
- Hardware details if relevant (Pi model, Arduino board, etc.)
