"""Capture actual SDL room UI with eight owned native players and one spectator."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', type=Path, default=Path('out/build/windows-debug/Release'))
    parser.add_argument('--output', type=Path, default=Path('out/m4-native-captures'))
    parser.add_argument('--timeout', type=float, default=45)
    args = parser.parse_args()
    args.bin = args.bin.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if os.name == 'nt' else ''
    client = str(args.bin / ('stackpulse_client' + suffix))
    server = str(args.bin / ('stackpulse_server' + suffix))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    processes, logs = [], []
    report = {'failure': None, 'captures': {}, 'clients': []}
    try:
        menu = subprocess.run([client, '--smoke-multiplayer-menu', '--smoke-frames', '8',
                               '--capture', str(args.output / 'menu.bmp'), '--mute',
                               '--data-dir', str(args.output / 'menu-data')],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              env=env, creationflags=flags, timeout=15)
        (args.output / 'menu.log').write_text(menu.stdout)
        if menu.returncode:
            raise RuntimeError(f'menu smoke failed ({menu.returncode}): {menu.stdout}')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        def launch(command, name, environment=None):
            log = (args.output / (name + '.log')).open('w')
            logs.append(log)
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                       env=environment, creationflags=flags)
            processes.append(process)
            return process
        serving = launch([server, '--port', str(port), '--mode', 'modern', '--players', '8'], 'server')
        time.sleep(.3)
        if serving.poll() is not None:
            raise RuntimeError('owned server exited before clients connected')
        clients = []
        for index in range(9):
            spectator = index == 0
            name = 'spectator' if spectator else f'player{index}'
            command = [client, '--connect', '127.0.0.1', '--port', str(port), '--mode', 'modern',
                       '--name', name.upper(), '--online-bot', '--online-exit-ticks', '2400',
                       '--online-ready-delay-ms', '4000', '--mute', '--data-dir', str(args.output / (name + '-data'))]
            if spectator:
                command += ['--spectate', '--online-capture-tick', '30', '--capture', str(args.output / 'spectator-grid.bmp')]
            elif index == 1:
                command += ['--online-capture-tick', '30', '--capture', str(args.output / 'player-grid.bmp')]
            elif index == 8:
                command += ['--capture', str(args.output / 'result-roster.bmp')]
            clients.append((name, launch(command, name, env)))
        deadline = time.monotonic() + args.timeout
        while any(process.poll() is None for _, process in clients):
            if time.monotonic() >= deadline:
                raise RuntimeError(f'owned native clients exceeded {args.timeout}s')
            if serving.poll() is not None:
                raise RuntimeError(f'owned server exited early ({serving.returncode})')
            time.sleep(.1)
        for log in logs:
            log.flush()
        pattern = r'AUTHORITATIVE_RESULT tick=(\d+) result=(winner|draw) hash=([0-9a-fA-F]+)'
        server_results = re.findall(pattern, (args.output / 'server.log').read_text())
        report['server_terminal'] = server_results[-1] if server_results else None
        for name, process in clients:
            content = (args.output / (name + '.log')).read_text()
            results = re.findall(pattern, content)
            report['clients'].append({'name': name, 'exit': process.returncode,
                                      'terminal': results[-1] if results else None,
                                      'live_capture': 'ONLINE_CAPTURE' in content})
        for filename in ('menu.bmp', 'player-grid.bmp', 'spectator-grid.bmp', 'result-roster.bmp'):
            path = args.output / filename
            report['captures'][filename] = path.stat().st_size if path.exists() else 0
        if not report['server_terminal'] or any(
                item['exit'] or item['terminal'] != report['server_terminal']
                for item in report['clients']):
            raise RuntimeError('one or more clients failed to match the server terminal result')
        if not all(report['captures'].values()):
            raise RuntimeError('one or more required SDL captures were not produced')
    except Exception as error:
        report['failure'] = str(error)
    finally:
        # Only terminate process handles created by this invocation.
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        for process in reversed(processes):
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return bool(report['failure'])


if __name__ == '__main__':
    raise SystemExit(main())
