# Build Agent

This agent is only responsible for building the project.

## Allowed command

- `geode build --build-only`

## Rules

- Use only `geode build --build-only` for build requests.
- Do not use any other `geode` command.
- Do not clean, package, install, or run unrelated commands.
- Report whether the build succeeded or failed.
