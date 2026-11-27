"""Package the native Linux x64 game, server and tools, then test the extracted payload."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import tarfile
import tempfile

BINARIES = ('stackpulse_client', 'stackpulse_server', 'stackpulse_diagnostic',
            'stackpulse_headless', 'stackpulse_replay_verify', 'stackpulse_modern_replay_verify',
            'stackpulse_determinism_matrix', 'stackpulse_room_load')
LAUNCHER = b'''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# WSLg's audio socket can appear a few seconds after a cold distro start.
wait_audio=1
for arg do
    case "$arg" in --mute|--list-drivers|--verify) wait_audio=0 ;; esac
done
if [ "$wait_audio" -eq 1 ] && [ -n "${WSL_INTEROP:-}" ] &&
   [ "${PULSE_SERVER:-}" = "unix:/mnt/wslg/PulseServer" ] &&
   [ "${SDL_AUDIO_DRIVER:-${SDL_AUDIODRIVER:-}}" != "dummy" ]; then
    attempt=0
    while [ ! -S /mnt/wslg/PulseServer ] && [ "$attempt" -lt 50 ]; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
fi
exec "$app_dir/stackpulse_client" "$@"
'''
README = b'''# Stackpulse Linux x64 game

Extract the whole archive and launch from a graphical desktop terminal:

```sh
./play.sh
./play.sh --mode fusion
```

The launcher forwards arguments and works from any current directory. The same
Classic, Fusion, Modern, Practice Lab, replay browser and multiplayer UI are
available on Linux. CLIENT_GUIDE.md describes controls and replay navigation.
Keyboard and controller bindings are configurable in Settings. No ROM or
additional game assets are needed. Preferences and replays use SDL's per-user
data directory; --data-dir PATH selects another writable directory.

For local multiplayer, start ./stackpulse_server in another terminal, then choose
Multiplayer. The default server address is 127.0.0.1:27877. Windows and Linux
clients use the same room protocol and should run matching builds.

SDL is linked into the client; system libraries are not bundled. Run in an X11
or Wayland desktop session with working graphics and audio libraries. SDL loads
some desktop/audio backends dynamically, so ldd alone does not describe every
runtime requirement. RUNTIME_DEPENDENCIES.txt records linked libraries, required
ELF symbol versions, and the compiled SDL drivers. Common desktop runtimes include
X11 or Wayland/xkbcommon, Mesa/OpenGL/EGL, and PulseAudio/PipeWire compatibility or
ALSA. Older distros may need newer C/C++ runtimes than they provide by default.

Under WSL, WSLg must be available for the graphical client. Physical controller
devices may need separate USB forwarding; dummy-driver checks do not test them.

Useful troubleshooting commands:

```sh
./play.sh --list-drivers
SDL_VIDEO_DRIVER=x11 SDL_RENDER_DRIVER=software ./play.sh
SDL_VIDEO_DRIVER=wayland ./play.sh
./play.sh --mute
```

The separate Linux server-only archive does not require a desktop or SDL.
SHA256SUMS.json hashes every payload file except itself. No settings, recordings,
test binaries or development files are included. The package is checked from a
fresh directory on its build host; this is not a clean-distro compatibility claim.
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, required=True)
    parser.add_argument('--sdl-license', type=Path, required=True)
    parser.add_argument('--enet-license', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/packages/stackpulse-linux-x64.tar.gz'))
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine().lower() not in ('x86_64', 'amd64'):
        parser.error('run inside the Linux x64 build environment')
    root = Path(__file__).resolve().parents[1]
    files, dependencies, versions = {}, {}, {}
    for name in BINARIES:
        source = (args.bin_dir / name).resolve()
        files[name] = source.read_bytes()
        linked = subprocess.run(['ldd', str(source)], capture_output=True, text=True, check=True)
        if 'not found' in linked.stdout:
            raise SystemExit('unresolved shared library: ' + name)
        dependencies[name] = re.sub(r' \(0x[0-9a-fA-F]+\)', '', linked.stdout)
        elf = subprocess.check_output(['readelf', '--version-info', str(source)], text=True)
        versions[name] = sorted(set(re.findall(r'Name: (\S+)', elf)))
    drivers = subprocess.check_output([str((args.bin_dir / 'stackpulse_client').resolve()),
                                       '--list-drivers'], text=True, env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
    video = [line.split(' ', 1)[1] for line in drivers.splitlines() if line.startswith('VIDEO_DRIVER ')]
    audio = [line.split(' ', 1)[1] for line in drivers.splitlines() if line.startswith('AUDIO_DRIVER ')]
    if not {'x11', 'wayland'}.intersection(video):
        raise SystemExit('client has no X11/Wayland driver; install desktop development libraries and reconfigure SDL')
    if not {'pulseaudio', 'pipewire', 'alsa', 'sndio', 'dsp'}.intersection(audio):
        raise SystemExit('client has no desktop audio backend')
    for name, source in (
        ('CLIENT_GUIDE.md', root / 'apps/client/README.md'),
        ('PACKAGING.md', root / 'docs/PACKAGING.md'),
        ('LINUX_CLIENT.md', root / 'docs/LINUX_CLIENT.md'),
        ('images/linux-client.png', root / 'docs/images/linux-client.png'),
        ('PROTOCOL.md', root / 'src/protocol/README.md'),
        ('THIRD_PARTY_NOTICES.md', root / 'THIRD_PARTY_NOTICES.md'),
        ('licenses/SDL-LICENSE.txt', args.sdl_license),
        ('licenses/SDL-hidapi-LICENSE.txt', args.sdl_license.parent / 'src/hidapi/LICENSE.txt'),
        ('licenses/SDL-hidapi-BSD.txt', args.sdl_license.parent / 'src/hidapi/LICENSE-bsd.txt'),
        ('licenses/SDL-hidapi-original.txt', args.sdl_license.parent / 'src/hidapi/LICENSE-orig.txt'),
        ('licenses/SDL-yuv2rgb-LICENSE.txt', args.sdl_license.parent / 'src/video/yuv2rgb/LICENSE'),
        ('licenses/ENet-LICENSE.txt', args.enet_license),
    ):
        files[name] = source.read_bytes()
    files['README.md'], files['play.sh'] = README, LAUNCHER
    glibc = subprocess.check_output(['ldd', '--version'], text=True).splitlines()[0]
    runtime = glibc + '\n\nCompiled SDL drivers:\n' + drivers + '\n'
    runtime += 'SDL desktop/audio drivers can load additional system libraries at runtime.\n\n'
    runtime += '\n'.join(name + '\n' + dependencies[name] +
                         'Required ELF versions: ' + ', '.join(versions[name]) + '\n' for name in BINARIES)
    files['RUNTIME_DEPENDENCIES.txt'] = runtime.encode()
    manifest = {name: hashlib.sha256(content).hexdigest() for name, content in sorted(files.items())}
    files['SHA256SUMS.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    executable = {*BINARIES, 'play.sh'}
    with args.output.open('wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as archive:
                for name, content in sorted(files.items()):
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.mtime = len(content), 1577836800
                    entry.mode = 0o755 if name in executable else 0o644
                    archive.addfile(entry, io.BytesIO(content))
    report = {'archive': str(args.output.resolve()), 'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
              'files': len(files), 'platform': platform.platform(), 'glibc': glibc,
              'video_drivers': video, 'audio_drivers': audio, 'elf_version_requirements': versions,
              'passed': False, 'smoke': []}
    report_path = args.output.with_name(args.output.name + '.validation.json')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    with tempfile.TemporaryDirectory(prefix='stackpulse client relocated ') as temp:
        location = Path(temp)
        seen = set()
        with tarfile.open(args.output) as archive:
            for entry in archive:
                if entry.name not in files or entry.name in seen or not entry.isfile():
                    raise SystemExit('archive payload mismatch')
                content = archive.extractfile(entry).read()
                if content != files[entry.name]:
                    raise SystemExit('archive payload checksum mismatch')
                destination = location / entry.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
                destination.chmod(entry.mode)
                seen.add(entry.name)
        if seen != files.keys():
            raise SystemExit('missing archive payload')
        work = location / 'different working directory'
        work.mkdir()
        home = location / 'home'
        home.mkdir()
        env = {'PATH': '/usr/bin:/bin', 'LANG': 'C', 'HOME': str(home),
               'XDG_DATA_HOME': str(home / 'data'), 'XDG_CONFIG_HOME': str(home / 'config'),
               'XDG_CACHE_HOME': str(home / 'cache'), 'SDL_VIDEO_DRIVER': 'dummy',
               'SDL_RENDER_DRIVER': 'software', 'SDL_AUDIO_DRIVER': 'dummy'}
        cases = [('headless', [str(location / 'stackpulse_headless')]),
                 ('server', [str(location / 'stackpulse_server'), '--port', '28997', '--ticks', '3'])]
        for mode in ('classic', 'fusion', 'modern'):
            cases.append((mode, [str(location / 'play.sh'), '--mode', mode, '--seed', '123',
                                  '--smoke-play', '--smoke-frames', '8', '--data-dir', str(home / mode),
                                  '--capture', str(location / (mode + '.bmp'))]))
        for name, command in cases:
            result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True, timeout=25)
            case = {'case': name, 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
            report['smoke'].append(case)
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            if result.returncode:
                print(json.dumps(case, indent=2))
                raise SystemExit('relocated smoke failed: ' + name)
            if name in ('classic', 'fusion', 'modern'):
                if 'Video backend: dummy' not in result.stdout or 'Audio backend: dummy' not in result.stdout:
                    raise SystemExit('client did not initialize the requested headless video/audio drivers')
                capture = (location / (name + '.bmp')).read_bytes()
                if len(capture) < 1000 or capture[:2] != b'BM':
                    raise SystemExit('client renderer capture missing or invalid')
                width, height = struct.unpack_from('<ii', capture, 18)
                if width != 1280 or abs(height) != 720:
                    raise SystemExit('unexpected client capture dimensions')
                case['capture_pixels'] = [width, abs(height)]
    report['passed'] = True
    report['smoke_scope'] = 'fresh directory with spaces, unrelated cwd, stripped environment, all modes with dummy video/audio; not a desktop or clean-distro test'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
