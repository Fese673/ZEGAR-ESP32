# core

Application core, bootstrap, runtime glue, and orchestration layer.

## Scope
- App boot and app loop glue
- Shared runtime values previously owned by main.cpp
- App state and mode management
- Cross-module diagnostics and runtime stats
- Core data flow between services

## Rules
- Keep business logic here, not hardware-specific code.
- Do not access device pins directly in this layer.
