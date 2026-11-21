"""Build an allowlisted, reproducible Windows ZIP and smoke it from a fresh directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

BINARIES = ('stackpulse_client', 'stackpulse_server', 'stackpulse_diagnostic',
            'stackpulse_headless', 'stackpulse_replay_verify', 'stackpulse_modern_replay_verify',
            'stackpulse_determinism_matrix', 'stackpulse_room_load')
RUNTIMES = {'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll',
            'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'concrt140.dll'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', required=True, type=Path)
    parser.add_argument('--dumpbin', required=True, type=Path)
    parser.add_argument('--redist-dir', type=Path, help='VS installed x64/Microsoft.VC*.CRT directory')
    parser.add_argument('--sdl-license', required=True, type=Path)
    parser.add_argument('--enet-license', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path('out/packages/stackpulse-windows-x64.zip'))
    parser.add_argument('--skip-smoke', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    files = {}
    queue = []
    for name in BINARIES:
        path = args.bin_dir / (name + '.exe')
        if not path.is_file():
            raise SystemExit(f'missing release executable: {path}')
        files[path.name] = path.read_bytes()
        queue.append(path)
    inspected = set()
    system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
    while queue:
        path = queue.pop()
        if path.name.lower() in inspected:
            continue
        inspected.add(path.name.lower())
        text = subprocess.check_output([str(args.dumpbin.resolve()), '/DEPENDENTS', str(path.resolve())], text=True)
        for dll in re.findall(r'^\s+([\w.-]+\.dll)\s*$', text, re.MULTILINE | re.IGNORECASE):
            name = dll.lower()
            if name.endswith('d.dll') and name.startswith(('vcruntime', 'msvcp', 'ucrtbase')):
                raise SystemExit('debug runtime dependency rejected: ' + dll)
            if name in RUNTIMES:
                runtime = args.redist_dir / name if args.redist_dir else None
                if runtime is None or not runtime.is_file():
                    raise SystemExit('supply installed licensed --redist-dir containing ' + name)
                files[name] = runtime.read_bytes()
                queue.append(runtime)
            elif name.startswith(('api-ms-win-', 'ext-ms-win-')) or (system / dll).is_file():
                continue
            else:
                raise SystemExit('unresolved non-system dependency: ' + dll)
    for target, source in (
        ('PROJECT_README.md', root / 'README.md'),
        ('CLIENT_GUIDE.md', root / 'apps/client/README.md'),
        ('PROTOCOL.md', root / 'src/protocol/README.md'),
        ('THIRD_PARTY_NOTICES.md', root / 'THIRD_PARTY_NOTICES.md'),
        ('protocol-v1.md', root / 'specs/protocol-v1.md'),
        ('PACKAGING.md', root / 'docs/PACKAGING.md'),
        ('licenses/SDL-LICENSE.txt', args.sdl_license),
        ('licenses/SDL-hidapi-LICENSE.txt', args.sdl_license.parent / 'src/hidapi/LICENSE.txt'),
        ('licenses/SDL-hidapi-BSD.txt', args.sdl_license.parent / 'src/hidapi/LICENSE-bsd.txt'),
        ('licenses/SDL-hidapi-original.txt', args.sdl_license.parent / 'src/hidapi/LICENSE-orig.txt'),
        ('licenses/SDL-yuv2rgb-LICENSE.txt', args.sdl_license.parent / 'src/video/yuv2rgb/LICENSE'),
        ('licenses/ENet-LICENSE.txt', args.enet_license),
    ):
        files[target] = source.read_bytes()
    if any(name in files for name in RUNTIMES):
        # Installed VS supplies this named redistribution notice; never glob licenses.
        notices = [parent / 'Licenses/1033/Redist.txt' for parent in args.redist_dir.parents]
        notice = next((path for path in notices if path.is_file()), None)
        if notice is None:
            raise SystemExit('installed Visual Studio Licenses/1033/Redist.txt not found')
        files['licenses/MSVC-REDIST.txt'] = notice.read_bytes()
    files['README.md'] = b'''# Stackpulse Windows x64 release

Extract the whole ZIP to a writable folder. Open PowerShell in that folder.
Launch the native client:

```powershell
.\\stackpulse_client.exe
```

Choose Fusion or Modern, then Multiplayer to create/join a room or spectate.
For a local dedicated room server in a second PowerShell window:

```powershell
.\\stackpulse_server.exe --bind 127.0.0.1 --port 27877 --mode fusion
```

Use 127.0.0.1 and port 27877 in the client. The server runs until Ctrl+C.
For a LAN server, bind to the host's LAN interface and allow its UDP port in
the firewall. This release does not provide public matchmaking or NAT traversal.

CLIENT_GUIDE.md describes controls, settings and multiplayer. PROTOCOL.md
describes the current room protocol. protocol-v1.md is an earlier draft.
PROJECT_README.md describes the source project; its source/build links apply
to a checkout, not this ZIP. PACKAGING.md explains provenance and verification.

Other programs: stackpulse_diagnostic.exe (input diagnostic),
stackpulse_headless.exe (original M0 deterministic diagnostic),
stackpulse_replay_verify.exe and stackpulse_modern_replay_verify.exe (replay
verification), stackpulse_determinism_matrix.exe (trace-v1 comparison CSV),
stackpulse_room_load.exe (local 24-peer capacity exercise; binds UDP 28992).
The replay tools require a replay path. The matrix defaults to 100 seeds and
100000 ticks per preset; the room load defaults to 1800 real seconds.

SHA256SUMS.json contains SHA256 hashes of every payload file except itself.
Applicable third-party notices are in THIRD_PARTY_NOTICES.md and licenses/.
No ROM, external art, user settings or replay files are supplied.
'''
    files['licenses/MSVC-RUNTIME-NOTICE.txt'] = (
        'Microsoft Visual C++ Runtime files, when present, were copied only from the installed '
        'Visual Studio REDIST x64 CRT directory. Redistribution is subject to the distributor\'s '
        'Visual Studio license and the Microsoft REDIST list. Windows system DLLs are not included.\n'
        'https://aka.ms/vs/18/redistribution\n').encode()
    manifest = {name: hashlib.sha256(content).hexdigest() for name, content in sorted(files.items())}
    files['SHA256SUMS.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, content in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    report = {'zip': str(args.output.resolve()), 'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
              'files': len(files), 'smoke': []}
    if not args.skip_smoke:
        with tempfile.TemporaryDirectory(prefix='stackpulse-relocated-') as temp:
            location = Path(temp)
            with zipfile.ZipFile(args.output) as archive:
                archive.extractall(location)
            for name, digest in manifest.items():
                if hashlib.sha256((location / name).read_bytes()).hexdigest() != digest:
                    raise SystemExit('extracted hash mismatch: ' + name)
            env = os.environ.copy()
            env.update(PATH=str(system), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       APPDATA=str(location / 'user'), LOCALAPPDATA=str(location / 'user'))
            commands = [('stackpulse_headless', []), ('stackpulse_server', ['--port', '28993', '--ticks', '3']),
                        ('stackpulse_client', ['--smoke-frames', '5', '--smoke-play', '--mode', 'fusion', '--mute',
                                              '--data-dir', str(location / 'user')])]
            for name, flags in commands:
                completed = subprocess.run([str(location / (name + '.exe')), *flags], cwd=location,
                                           env=env, capture_output=True, text=True, timeout=20)
                report['smoke'].append({'binary': name, 'returncode': completed.returncode,
                                        'stdout': completed.stdout, 'stderr': completed.stderr})
                if completed.returncode:
                    print(json.dumps(report, indent=2))
                    raise SystemExit('relocated smoke failed: ' + name)
        report['smoke_scope'] = 'fresh temporary directory, stripped PATH, same Windows host; not a clean-machine VM'
    args.output.with_suffix('.validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
