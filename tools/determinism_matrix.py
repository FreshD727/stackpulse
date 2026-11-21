"""Run and compare trace-v1 CSV matrices from any platform/configuration."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', action='append', default=[], help='LABEL=executable; repeat for each configuration')
    parser.add_argument('--compare', action='append', default=[], help='existing matrix CSV; repeat for CI artifacts')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--seeds', type=int, default=100)
    parser.add_argument('--ticks', type=int, default=100000)
    args = parser.parse_args()
    if args.seeds < 1 or args.ticks < 1:
        parser.error('seeds and ticks must be positive')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    paths = [Path(p) for p in args.compare]
    for item in args.binary:
        label, executable = item.split('=', 1)
        if not label or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in label):
            parser.error('labels must contain only ASCII letters, digits, hyphen or underscore')
        path = args.output_dir / (label + '.csv')
        subprocess.run([str(Path(executable).resolve()), '--seeds', str(args.seeds), '--ticks', str(args.ticks),
                        '--output', str(path.resolve())], check=True)
        paths.append(path)
    if not paths:
        parser.error('provide --binary or --compare')
    baseline = None
    records = []
    for path in paths:
        with path.open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream))
        keys = {(r['preset'], int(r['seed'])) for r in rows}
        expected = {(mode, seed) for mode in ('fusion', 'modern') for seed in range(1, args.seeds + 1)}
        if keys != expected or len(rows) != len(expected) or any(int(r['ticks']) != args.ticks for r in rows):
            raise SystemExit(f'incomplete matrix: {path}')
        canonical = sorted(rows, key=lambda row: (row['preset'], int(row['seed'])))
        if baseline is not None and canonical != baseline:
            raise SystemExit(f'determinism mismatch: {path}')
        baseline = canonical
        records.append({'file': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'rows': len(rows)})
    report = {'trace_version': 1, 'seeds': args.seeds, 'ticks': args.ticks, 'compared': len(paths),
              'comparison_passed': len(paths) > 1, 'matrices': records}
    (args.output_dir / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
