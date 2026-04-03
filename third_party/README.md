# third_party

External or legacy code that is not owned as first-party firmware logic.

## Structure
- `vendor/` - active external dependencies integrated into the project
- `archive/` - historical snapshots kept for reference

## Rule
Do not mix first-party application code into `third_party/`.
