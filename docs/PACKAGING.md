# Release packages

The private alpha has two distribution artifacts under `out/packages/`:
`stackpulse-windows-x64.zip` (client, server and tools) and
`stackpulse-linux-x64-server.tar.gz` (server and tools). Keep the matching
`.validation.json` reports alongside them. These generated files are ignored by
Git; upload archives as release assets rather than checking binaries into source.
The local `out/release/` directory is the latest extracted Windows package and
can be launched without a source build. Development builds are recreated under
`out/build/` using the presets.

Before sharing a release, build and test the final source, regenerate both
packages, inspect their validation reports and launch the actual extracted
artifacts. The scripts retain dependency licenses and manifests. Do not describe
a relocated same-host smoke as a clean-machine or cross-distro compatibility test.
Creating a GitHub release or publishing assets is a separate manual action.

## Windows release package

Build all targets in **Release**, then run `python tools/package_release.py` with
`--bin-dir out/build/windows-debug/Release` (the Release configuration of the
existing multi-configuration build), the installed Visual Studio x64
`--dumpbin` executable, `--redist-dir` pointing to the installed x64
`Microsoft.VC145.CRT` directory, `--sdl-license` pointing to the pinned SDL source
`LICENSE.txt`, and `--enet-license` pointing to the pinned ENet source `LICENSE`.
The default output is `out/packages/stackpulse-windows-x64.zip`.

For this host the Visual Studio installation is
`C:/Program Files/Microsoft Visual Studio/18/Community`; toolchain version
`14.51.36231` has `VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/dumpbin.exe` and
`VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT`.
Discover the installed path rather than assuming this version on another host.
Only redistribute runtime files for which the distributor's Visual Studio
license permits redistribution; the script accepts only the named VC runtime
DLLs from that explicitly supplied redist directory. It never copies Windows
system DLLs, debug CRTs, or arbitrary DLLs from PATH. `dumpbin /DEPENDENTS`
checks every shipped executable and copied runtime recursively. Unresolved
non-system dependencies fail packaging.
When VC runtime DLLs are included, the installed Visual Studio
`Licenses/1033/Redist.txt` redistribution notice is included as well.

The package uses an explicit list: client, server, input diagnostic, original M0
headless diagnostic, both replay verification tools, determinism matrix tool,
room load tool, a package-specific startup README, source-project README,
client guide, current room protocol guide, earlier protocol draft, this guide,
dependency notices and SDL/ENet license texts. Art, fonts and tones are generated or supplied by SDL; no
ROM assets are included. No replay, user settings, secrets, source build trees,
test executables or PDBs are eligible for packaging. Catch2 is test-only and is
not linked to these release executables.

ZIP entries are sorted, stored without compression, assigned a fixed timestamp
and fixed permission bits. `SHA256SUMS.json` hashes all payload files except
itself. The external `.validation.json` records the complete ZIP SHA256 and
smoke results. Identical input file bytes produce identical ZIP bytes. This does
not promise bit-identical compiler output across separate builds or toolchains.
`out/` and ZIP files are ignored by Git.

Unless `--skip-smoke` is supplied, the script extracts to a fresh temporary
directory, verifies every payload hash, strips PATH to Windows System32, and
runs the headless diagnostic, a three-tick dedicated server, and a five-frame
Fusion client with dummy SDL video/audio. It redirects user configuration paths
to that temporary directory. Every process has a bounded timeout. A successful
run is a relocated smoke test on the same host; it is not proof of a clean VM
or an unmodified machine without a developer toolchain. A clean Windows VM is
still the appropriate release acceptance check.

## Determinism matrix

`stackpulse_determinism_matrix --seeds 100 --ticks 100000 --output matrix.csv`
runs both Fusion and Modern. Trace v1 specifies xorshift32 seeded with
`seed XOR 0xa511e9b3`, once per tick. Input masks are generated with integer
operations in `tools/determinism_matrix.cpp`; there is no platform random
library, wall clock, floating-point arithmetic, or unordered traversal.
Each canonical serialized state contributes every byte to a rolling FNV-1a
hash. The final state hash is recorded separately. Topout states are hashed and
then a fresh episode starts with `seed + episode * 0x9e3779b9` modulo 2^32.
This deliberately exercises live gameplay throughout the trace instead of
hashing an unchanged terminal board for most of 100000 ticks. Episode and lock
counts are included in the CSV. Every 997 ticks a checkpoint is decoded,
compared with the original, advanced by one identical input, and checked again.

Run Windows Debug and Release binaries with:

```text
python tools/determinism_matrix.py --binary windows-debug=out/build/windows-debug/Debug/stackpulse_determinism_matrix.exe --binary windows-release=out/build/windows-debug/Release/stackpulse_determinism_matrix.exe --output-dir out/determinism
```

For Linux, configure separate Debug/Release build directories with client,
network and tests disabled; no third-party downloads are needed:

```sh
cmake -S . -B out/build/linux-m4-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTACKPULSE_BUILD_CLIENT=OFF -DSTACKPULSE_BUILD_NETWORK=OFF -DSTACKPULSE_BUILD_TESTS=OFF
cmake --build out/build/linux-m4-release --target stackpulse_determinism_matrix
out/build/linux-m4-release/stackpulse_determinism_matrix --output out/determinism/linux-release.csv
```

Repeat with `Debug` and a different directory/output name. Download CSVs from CI
or copy them from the Linux build and pass all four `--compare FILE` arguments
to `tools/determinism_matrix.py --output-dir out/determinism`. The runner rejects
missing/duplicate seed-preset rows, unequal requested tick counts and any field
mismatch. `comparison.json` identifies the CSV hashes and comparison scope; a
single matrix is recorded as unpaired, never as a successful comparison.

## Linux server package

Inside the Linux build environment, build the server/tools with the SDL client
disabled, then run:

```sh
python3 tools/package_linux_server.py --bin-dir out/build/linux-m4-network --enet-license out/deps/enet-src/LICENSE
```

The default output is `out/packages/stackpulse-linux-x64-server.tar.gz`. Its
explicit allowlist contains the dedicated server, original headless diagnostic,
two replay verifiers, matrix tool, room load tool, server startup README,
current protocol guide, packaging guide and ENet notices/license. The archive
fixes file order, modes, ownership and timestamps; gzip records no filename or
wall-clock timestamp. `ldd` output is included with ASLR addresses removed.
Required ELF symbol versions are recorded with `readelf --version-info`.
The manifest and external validation report record payload/archive SHA256 hashes.
Only original binaries and named text files are included.

Linux packages depend on the deployment distro's compatible glibc, libstdc++,
libgcc and ELF loader; system libraries are not bundled. The package records
the build host's glibc version and shared-library dependencies. A relocated
test extracts the actual tar payload to a fresh temporary directory, strips
the environment, and runs the server for three ticks and the headless tool.
This checks the existing Ubuntu WSL host; it does not establish compatibility
with other distros, older glibc versions, or a clean VM.

## Room soak

`python tools/room_soak.py --binary out/build/windows-debug/Release/stackpulse_room_load.exe --seconds 1800 --output out/soak/room-soak.json`
launches the native authenticated ENet load harness, which owns its server and
clients. The wrapper records actual monotonic wall duration and host platform
alongside native metrics. It never labels accelerated simulation ticks as
thirty minutes of wall time. Native metric definitions and room population
must be checked in the recorded output before accepting the capacity result.

From a source checkout, `python tools/native_room_smoke.py --bin out/build/windows-debug/Release --output out/m4-native-captures`
runs an owned native server, eight SDL player processes and one SDL spectator.
It captures the multiplayer menu, player/spectator grids and result roster,
then compares the native clients' terminal tick/result/hash with the server.
This complements the 24-peer transport soak and package relocation check.
The QA Python script is a source-checkout tool and is not included in the
binary package.
