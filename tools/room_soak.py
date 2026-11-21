"""Run the native real-ENet room capacity load for a measured wall-clock interval."""
import argparse
import json
from pathlib import Path
import platform
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--port', type=int, default=28992)
    parser.add_argument('--mode', choices=('fusion', 'modern'), default='fusion')
    parser.add_argument('--output', type=Path, default=Path('out/soak/room-soak.json'))
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('seconds must be positive')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    native = args.output.with_suffix('.native.json')
    command = [str(args.binary.resolve()), '--seconds', str(args.seconds), '--port', str(args.port),
               '--mode', args.mode, '--output', str(native.resolve())]
    start = time.monotonic()
    completed = subprocess.run(command, capture_output=True, text=True, timeout=args.seconds + 120)
    elapsed = time.monotonic() - start
    metrics = json.loads(native.read_text()) if native.is_file() else None
    capacity_passed = (completed.returncode == 0 and metrics is not None and metrics.get('passed') is True
                       and metrics.get('players') == 8 and metrics.get('spectators') == 16
                       and metrics.get('maximum_connections', 0) == 24
                       and metrics.get('completed_matches', 0) > 0
                       and metrics.get('terminal_convergence_checks', 0) >= 24)
    report = {'command': command, 'requested_seconds': args.seconds, 'wall_seconds': elapsed,
              'platform': platform.platform(), 'processor': platform.processor(),
              'returncode': completed.returncode, 'stdout': completed.stdout, 'stderr': completed.stderr,
              'native': metrics, 'capacity_passed': capacity_passed,
              'thirty_minute_wall_run': capacity_passed and metrics.get('elapsed_seconds', 0) >= 1800
                                       and args.seconds >= 1800}
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    raise SystemExit(completed.returncode or (0 if capacity_passed else 1))


if __name__ == '__main__':
    main()
