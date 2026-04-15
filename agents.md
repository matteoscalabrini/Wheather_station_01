# Agents Guidelines

## Collaboration Protocol

- **Present options, don't impose them.** When a request is open-ended (e.g., "how should I connect 9 displays?"), present all reasonable approaches with trade-offs before choosing one.
- **Wait for explicit approval** before making architectural decisions or adding/removing hardware dependencies. The user's hardware situation is their own — never assume what components they have.
- **Scope changes require consent.** If a task expands beyond what was originally asked, pause and present the expanded plan first.
- **Prefer native capabilities over external components.** The ESP32 has many free GPIO pins and supports multiple software I2C buses. Consider using what the MCU already provides before introducing external ICs (multiplexers, expanders, etc.).

## Project Conventions

- All board configuration lives in `include/board_config.h` as `constexpr` values in the `BoardConfig` namespace.
- All firmware logic lives in `src/main.cpp`.
- The README must always reflect the actual state of the firmware — no aspirational descriptions.
- Build with `~/.platformio/penv/bin/pio run` before declaring work done.