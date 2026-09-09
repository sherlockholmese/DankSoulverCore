# Dank SoulverCore

A DankMaterialShell launcher plugin that evaluates natural-language calculations with
[SoulverCore](https://github.com/soulverteam/SoulverCore). Valid answers are merged into the
normal DMS app-launcher results and are copied to the clipboard when selected.

The plugin is automatic by default—there is no calculator window and no required prefix.
It waits until the search has at least three characters, evaluates locally, and stays invisible
when Soulver does not produce a useful result. You can switch to an explicit `=` trigger in the
plugin settings.

Examples:

- `15% of 80`
- `forty two plus eight`
- `12 km in miles`
- `40 SGD in USD`
- `time in New York in 2 hours`
- `3 days after next Friday`

## Architecture

`SoulverLauncher.qml` implements the DMS launcher-provider interface. It debounces the live
launcher text and talks over JSON Lines to one persistent `dank-soulver-core --server` process,
so SoulverCore is initialized once rather than once per keystroke. The helper uses the C ABI
provided by [vicinaehq/soulver-cpp](https://github.com/vicinaehq/soulver-cpp).

The installed soulver-cpp wrapper contains the Raycast currency provider used by Vicinae. It
refreshes fiat and selected crypto rates from Raycast's backend hourly. Expressions themselves
stay local; only currency-rate requests are made to Raycast.

## Requirements

- DankMaterialShell 1.2.0 or newer
- CMake 3.20+, Ninja, and a C++20 compiler (build time)
- `nlohmann-json`
- `soulver-cpp`, including its Swift runtime, `libSoulverWrapper.so`,
  `libSoulverCoreDynamic.so`, and resource bundle

On Arch Linux, soulver-cpp documents this installation route:

```sh
paru -S swift-bin-6.1 soulver-cpp-git
```

SoulverCore itself is closed source. Review its license before redistributing its library or
resources; this repository does not vendor either one.

## Build, test, and package

```sh
make test
make package
```

`make package` compiles the helper, runs after the same build used by the tests, and creates a
clean, copy-ready plugin directory at:

```text
dist/DankSoulverCore/
```

Generated `build/`, `bin/`, and `dist/` directories are intentionally ignored by Git. For a
system-wide helper instead:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

The helper searches both `soulver-core/resources` and `soulver-cpp/resources` beneath standard
XDG data directories. You can override discovery with `SOULVER_RESOURCES` or the plugin's
**Soulver resources** setting.

## Install in DankMaterialShell

1. Install the requirements above.
2. From this repository, run `make test` and then `make package`.
3. Copy the generated `dist/DankSoulverCore` directory into:

```text
~/.config/DankMaterialShell/plugins/DankSoulverCore
```

After copying, this path must exist (avoid an extra nested directory):

```text
~/.config/DankMaterialShell/plugins/DankSoulverCore/plugin.json
```

4. Ask a running DMS session to discover the copied plugin:

```sh
dms ipc plugin-scan scan
```

   If DMS is not running, simply start or restart it instead.

5. Open **Settings → Plugins** and enable **Dank SoulverCore**.
6. In the launcher plugin settings, allow Dank SoulverCore to run without a trigger. DMS requires
   this explicit opt-in before any plugin can merge results into ordinary application searches.

Without that opt-in, turn off **Automatic results** in the plugin settings and use `=`, for
example `= 15% of 80`.

The local plugin metadata is already included in `plugin.json`; no additional metadata file is
needed when installing by copy.

### Essential runtime files

The copied plugin needs these files:

```text
DankSoulverCore/
├── plugin.json
├── SoulverLauncher.qml
├── SoulverService.qml
├── SoulverSettings.qml
├── run-helper
└── bin/
    └── dank-soulver-core
```

`README.md` and `LICENSE` are useful documentation but are not required by DMS at runtime.
The source and build files (`src/`, `CMakeLists.txt`, and `Makefile`) are only needed to compile
the helper and do not need to be copied. The installed soulver-cpp/Swift libraries and Soulver
resource bundle remain system dependencies; do not copy them into the plugin directory.

The plugin's `run-helper` entry point automatically prefers the packaged binary in `bin/`, then
falls back to `dank-soulver-core` in `PATH`. The **Helper executable** setting is only needed for
a custom location.

### Updating

Pull or copy the new repository version, run `make test && make package`, replace the existing
`~/.config/DankMaterialShell/plugins/DankSoulverCore` directory with the newly generated
`dist/DankSoulverCore` directory, and then run:

```sh
dms ipc plugin-scan rescan dankSoulverCore
```

## Helper protocol

One-shot use:

```sh
./bin/dank-soulver-core --json "15% of 80"
```

Persistent mode accepts one JSON object per line:

```json
{"id":1,"expression":"forty two plus eight"}
```

Every response repeats `id` and `expression`, which lets the QML side discard stale results when
the user keeps typing.

## Credits

The Linux Soulver bridge and Raycast currency provider originate in
[vicinaehq/soulver-cpp](https://github.com/vicinaehq/soulver-cpp), building on work from Flare.
The live-result behavior follows Vicinae's calculator integration while using DMS's native
launcher-plugin API.
