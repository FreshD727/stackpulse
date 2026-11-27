"""Create an allowlisted deterministic Linux server/tools tarball and relocated smoke."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import platform
import re
import subprocess
import tarfile
import tempfile

BINARIES = ('stackpulse_server', 'stackpulse_headless', 'stackpulse_replay_verify',
            'stackpulse_modern_replay_verify', 'stackpulse_determinism_matrix', 'stackpulse_room_load')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, required=True)
    parser.add_argument('--enet-license', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/packages/stackpulse-linux-x64-server.tar.gz'))
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('run this script inside the Linux build environment')
    root = Path(__file__).resolve().parents[1]
    files, dependencies, versions = {}, {}, {}
    for name in BINARIES:
        source = args.bin_dir / name
        files[name] = source.read_bytes()
        audited = subprocess.run(['ldd', str(source.resolve())], capture_output=True, text=True, check=True)
        if 'not found' in audited.stdout:
            raise SystemExit('unresolved shared library: ' + name)
        dependencies[name] = re.sub(r' \(0x[0-9a-fA-F]+\)', '', audited.stdout)
        elf = subprocess.check_output(['readelf', '--version-info', str(source.resolve())], text=True)
        versions[name] = sorted(set(re.findall(r'Name: (\S+)', elf)))
    for name, source in (('PROTOCOL.md', root / 'src/protocol/README.md'),
                         ('PACKAGING.md', root / 'docs/PACKAGING.md'),
                         ('THIRD_PARTY_NOTICES.md', root / 'THIRD_PARTY_NOTICES.md'),
                         ('licenses/ENet-LICENSE.txt', args.enet_license)):
        files[name] = source.read_bytes()
    files['README.md'] = b'''# Stackpulse Linux x64 server and headless tools

Extract this archive and run from a terminal:

```sh
./stackpulse_server --bind 127.0.0.1 --port 27877 --mode fusion
```

The server runs until Ctrl+C. Connect a Windows or Linux native client to this address
and UDP port. Each room selects its own Classic, Fusion or Modern preset. For LAN use bind to the server's LAN
interface and allow its UDP port in the firewall. No NAT traversal or public
matchmaking is supplied. PROTOCOL.md documents the current room protocol.

This archive contains no SDL client. It includes the original M0 headless
diagnostic, Classic/Modern replay verifiers (supply a replay path), trace-v1
determinism matrix, and native 24-peer room load tool (1800 seconds by default).

This is a dynamically linked build. The target distro must supply a compatible
glibc, libstdc++, libgcc and ELF loader. RUNTIME_DEPENDENCIES.txt records the
build-host ldd output and glibc version; those host paths are diagnostic, not
archive requirements. glibc and system libraries are not bundled. Test on your
deployment distro before use. PACKAGING.md records the relocated test scope.
SHA256SUMS.json hashes every payload file except itself. No ROM assets, secrets,
user configuration, or replays are supplied.
'''
    version = subprocess.check_output(['ldd', '--version'], text=True).splitlines()[0]
    files['RUNTIME_DEPENDENCIES.txt'] = (version + '\n' + '\n'.join(name + '\n' + dependencies[name] +
                                                                    'Required ELF versions: ' + ', '.join(versions[name]) + '\n'
                                                                    for name in BINARIES)).encode()
    manifest = {name: hashlib.sha256(content).hexdigest() for name, content in sorted(files.items())}
    files['SHA256SUMS.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as archive:
                for name, content in sorted(files.items()):
                    info = tarfile.TarInfo(name)
                    info.size, info.mtime = len(content), 1577836800
                    info.mode = 0o755 if name in BINARIES else 0o644
                    archive.addfile(info, io.BytesIO(content))
    report = {'archive': str(args.output.resolve()), 'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
              'files': len(files), 'platform': platform.platform(), 'glibc': version,
              'elf_version_requirements': versions, 'smoke': []}
    with tempfile.TemporaryDirectory(prefix='stackpulse-linux-relocated-') as temp:
        location = Path(temp)
        # Extract only the explicit payload allowlist, rejecting links and paths.
        seen = set()
        with tarfile.open(args.output) as archive:
            for entry in archive:
                if entry.name not in files or entry.name in seen or not entry.isfile():
                    raise SystemExit('archive payload mismatch')
                content = archive.extractfile(entry).read()
                if content != files[entry.name]:
                    raise SystemExit('archive payload mismatch')
                seen.add(entry.name)
                destination = location / entry.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
                destination.chmod(entry.mode)
        if seen != files.keys():
            raise SystemExit('missing archive payload')
        for name, flags in (('stackpulse_headless', []), ('stackpulse_server', ['--port', '28995', '--ticks', '3'])):
            completed = subprocess.run([str(location / name), *flags], cwd=location,
                                       env={'PATH': '/usr/bin:/bin', 'LANG': 'C'},
                                       capture_output=True, text=True, timeout=20)
            report['smoke'].append({'binary': name, 'returncode': completed.returncode,
                                    'stdout': completed.stdout, 'stderr': completed.stderr})
            if completed.returncode:
                raise SystemExit('relocated smoke failed: ' + name)
    report['smoke_scope'] = 'fresh temporary directory, stripped environment, same Ubuntu WSL host; not a clean distro/VM'
    args.output.with_name(args.output.name + '.validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
