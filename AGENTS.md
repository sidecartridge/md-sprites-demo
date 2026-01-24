# AGENTS.md
This repository contains **md-sprites-demo** (GPU Demo), a SidecarTridge Multi-device
microfirmware application that showcases the RP2040's capabilities as a graphics
co-processor for Atari ST/STE/Mega ST/Mega STE computers. The demo renders sprites
and tiles using the RP2040 (Raspberry Pi Pico W), which fills Atari-friendly
framebuffers in its memory that the Atari reads and displays. It follows the standard
split between RP2040 firmware (`rp/`) and target-computer firmware (`target/atarist/`).
See `README.md` for user-facing notes and the official SidecarTridge docs for
programming guidance.

---
## Project layout (important)
- `rp/` — RP2040-side firmware (graphics rendering, framebuffer management, main loop).
- `target/atarist/` — Target-computer firmware (framebuffer copying, display logic).
  Built with assembly (`main.s`), uses blitter on STE for fast copies.
- Submodules at repo root (do not vendor these):
  - `pico-sdk/`
  - `pico-extras/`
- `desc/` — App metadata template (`app.json`) used by root build script.
- `dist/` — Build artifacts (UF2, JSON, md5sum).
**Key coupling:** `target/atarist/build.sh` generates `target_firmware.h` and
copies it into `rp/src/include/`. If you change anything under `target/`, rebuild
so the embedded header stays in sync.

---
## Entry points
- `rp/src/main.c` — firmware entry point.
- `rp/src/emul.c` — app logic and main loop.
- `target/atarist/` — target firmware sources and build scripts.

---
## Build prerequisites
- ARM embedded toolchain (`arm-none-eabi-*`).
- CMake 3.26+ (used by `rp/src/CMakeLists.txt`).
- Python (for `target/atarist/firmware.py` binary embedding).
- `stcmd` on PATH (used by `target/atarist/build.sh` to assemble target firmware).
- Git submodules initialized (`pico-sdk`, `pico-extras`).

---
## Build commands
### Full build (recommended)
```sh
./build.sh <board_type> <build_type> <app_uuid_key>
```
Example:
```sh
./build.sh pico_w debug 44444444-4444-4444-8444-444444444444
```
What it does:
- Copies `version.txt` into `rp/` and `target/`.
- Builds target firmware (always `release`) and generates
  `rp/src/include/target_firmware.h`.
- Builds RP firmware.
- Stages artifacts into `dist/` and fills `desc/app.json`.
Expected artifacts in `dist/`:
- `<APP_UUID_KEY>-<VERSION>.uf2`
- `<APP_UUID_KEY>.json`
- `rp.uf2.md5sum`
### Target-only build (regenerate embedded firmware)
```sh
./target/atarist/build.sh "$(pwd)/target/atarist" release
```
Notes:
- Produces `target/atarist/dist/FIRMWARE.IMG` and updates
  `rp/src/include/target_firmware.h`.
- Requires `stcmd` on PATH.
### RP-only build
```sh
cd rp
./build.sh <board_type> <build_type>
```
Notes:
- `rp/build.sh` **pins submodule tags** (`pico-sdk` to `2.1.1`, `pico-extras` to
  `sdk-2.1.1`), and deletes `rp/build/`.
- `rp/build.sh` always configures with `-DCMAKE_BUILD_TYPE=Debug` regardless of
  the passed build type parameter.
- Both `rp/build.sh` and the root `build.sh` delete/replace `dist/`.

---
---
## Linting and formatting
- `rp/src/CMakeLists.txt` enables `clang-tidy` automatically when present.
- `clang-format` is wired as a CMake target in `rp/src`.
Recommended flow:
```sh
cd rp
./build.sh <board_type> <build_type>
cmake --build build --target clang-format
```
Single-file formatting:
```sh
clang-format -i rp/src/<file>.c
```
Notes:
- `rp/src/CMakeLists.txt` sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON` so
  `rp/build/compile_commands.json` exists after configure.
- There is no standalone lint script; build-time `clang-tidy` is the norm.

---
## Tests
- There is **no first-party test suite** in this repo.
- Validation is typically done by building the target and RP firmware.
- Submodules (e.g. `pico-sdk/`) contain their own tests; they are not part of the
  app workflow.
**Single-test note:** there is no test runner for this app. If you must run a
single SDK test, use the SDK’s own CMake targets under its `test/` directory.

---
## Code style guidelines
Follow `.clang-format` and `.clang-tidy` in the repo root.
### Formatting
- 2-space indent, 80-column limit.
- `Attach` style braces, no tabs.
- Pointer alignment is left (`type* ptr`).
- Keep include blocks sorted and grouped; clang-format enforces ordering.
### Naming (from `.clang-tidy`)
- Namespaces: `CamelCase`.
- Classes/structs: `CamelCase`.
- Functions: `camelBack` with a capitalized suffix (regex enforced).
- Variables/parameters: `camelBack`.
- Members: `camelBack` with `m_` prefix for private members.
### Types and constants
- Prefer fixed-width types (`uint32_t`, `int16_t`) for firmware interfaces.
- Use `bool` for boolean logic; avoid implicit integer truthiness.
- Avoid magic numbers; `0`, `1`, `-1` are allowed per `.clang-tidy`.
### Imports/includes
- Standard/system headers before project headers.
- Keep include blocks minimal; prefer forward declarations in headers.
- Don’t reorder includes manually if `clang-format` will do it.
### Error handling
- Check return codes and null pointers from hardware/SDK calls.
- Propagate errors upward rather than silently ignoring failures.
- Prefer explicit error logs over bare asserts in runtime paths.

---
## Workflow rules for agents
- Do not discard or overwrite local changes without explicit user approval.
- Avoid running `./build.sh` or `rp/build.sh` unless asked; they delete build
  artifacts and pin submodule tags.
- You may compile targets directly (for verification) without invoking the build
  scripts.
- If you change protocol or target-side code, rebuild target firmware so
  `rp/src/include/target_firmware.h` is regenerated.
- Respect `.clang-format` and `.clang-tidy` in the repo root.
- Do not change submodule pins unless explicitly requested.
- No Cursor or Copilot rule files are present in this repo.

---
## “Done” checklist
- Targets compile successfully without using `./build.sh` or `rp/build.sh`.
- If target firmware changed, confirm `rp/src/include/target_firmware.h` was
  regenerated.
