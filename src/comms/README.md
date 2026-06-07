# comms

Communication services and connectivity control.

## Scope
- Wi-Fi synchronization
- MQTT publish and transport logic
- Radio mode switching
- Central orchestration for WiFi/MQTT/radio ownership (`NetworkOrchestrator`)

## Rules
- Keep protocol logic isolated from UI rendering.
- Connection retries and state machines live here.
