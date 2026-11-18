# C++ formatting

The repository uses clang-format **22.1.3** and the root `.clang-format` file.
The style uses four spaces, a 100-column limit, and separate lines for control
statements so gameplay and UI logic stay readable. CI checks the same version.

Install the pinned formatter with Python 3.10 or newer:

```sh
python -m pip install clang-format==22.1.3
```

Alternatively put that version of `clang-format` on `PATH`, or set `CLANG_FORMAT`
to its executable path. For example, in PowerShell:

```powershell
$env:CLANG_FORMAT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
```

From the repository root, format tracked project C++ files or check them:

```sh
python tools/format_cpp.py
python tools/format_cpp.py --check
```

The script includes `.cpp` and `.hpp` files tracked by Git under `apps`, `include`,
`src`, `tests`, and `tools`. Add new source files to Git before running it. It does
not traverse dependencies, build output, or other external source trees.

## Optional pre-commit check

Install a check for this clone only:

```sh
python tools/format_cpp.py --install-hook
```

This writes a local `pre-commit` hook into the repository's hooks directory. It
does not change global Git settings or `core.hooksPath`, and refuses to overwrite
an existing hook or install when `core.hooksPath` is set. If you already use hooks, add this
command to your existing pre-commit workflow instead:

```sh
python tools/format_cpp.py --check --staged
```

The hook checks the staged snapshot, including partially staged files, and never
edits or stages files. When it reports a failure, format the working file, review
the changes, and stage the intended content again. Installation verifies the
formatter version and records absolute paths to that formatter and the Python
interpreter, so editor Git integrations do not need a separate `PATH` setup.
Reinstall the hook if either tool moves. To opt out, remove only the hook
installed by this command.

The local pre-commit check was installed for this working clone during issue-1
resolution. It binds the discovered Python and formatter executable paths; new
clones opt in with the install command above.
