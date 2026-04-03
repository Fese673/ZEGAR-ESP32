# core

Application core and orchestration layer.

## Scope
- App state and mode management
- Cross-module diagnostics and runtime stats
- Core data flow between services

## Rules
- Keep business logic here, not hardware-specific code.
- Do not access device pins directly in this layer.
