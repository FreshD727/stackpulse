"""Structural contract checks; no gameplay implementation or ROM dependency."""
import copy
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "out" / "python-deps"))
from jsonschema import Draft202012Validator

schema = json.loads((ROOT / "rules/schema-v1.json").read_text(encoding="utf-8"))
example = json.loads((ROOT / "rules/modern-v1.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
validator.validate(example)
invalid = copy.deepcopy(example)
invalid["unexpected"] = True
assert not validator.is_valid(invalid), "schema must reject unknown fields"
for field, value in [("preset", "classic"), ("lock_ticks", 0), ("hold", False)]:
    invalid = copy.deepcopy(example)
    invalid[field] = value
    assert not validator.is_valid(invalid), f"schema accepted invalid {field}"
for path in ROOT.rglob("*"):
    if not path.is_file() or any(part in {"out", ".git", "build"} for part in path.relative_to(ROOT).parts):
        continue
    # Preserve the user report byte-for-byte, including its missing final newline.
    if path.relative_to(ROOT).as_posix() == "docs/issues/issue-1-original.md":
        continue
    if path.suffix in {".md", ".cpp", ".hpp", ".json", ".py", ".yml", ".txt"}:
        content = path.read_text(encoding="utf-8")
        assert "\t" not in content, f"tab: {path}"
        assert all(line == line.rstrip() for line in content.splitlines()), f"trailing whitespace: {path}"
        assert content.endswith("\n"), f"missing newline: {path}"
print("Rules schema/example and source whitespace checks passed")

# Independently authored geometry-only T-spin triple: validates the proposed
# representative fixture, not an implemented falling-block game/tick runner.
fixture = json.loads((ROOT / "tests/fixtures/rotation/t-spin-triple-v1.json").read_text(encoding="utf-8"))
board = {(x, y) for y in (17, 18, 19) for x in range(10)
         if x != 4 and not (y == 18 and x == 5)}
board.add((4, 15))
assert len(fixture["board"]) == 40
assert all(len(row) == 10 for row in fixture["board"])
assert board == {(x, row - 20) for row, values in enumerate(fixture["board"])
                 for x, value in enumerate(values) if value}
orientation_0 = [(1, 0), (0, 1), (1, 1), (2, 1)]
orientation_r = [(1, 0), (1, 1), (2, 1), (1, 2)]
origin = (4, 15)
def cells(shape, at):
    return {(x + at[0], y + at[1]) for x, y in shape}
def collision(shape, at):
    return any(x < 0 or x >= 10 or y < -20 or y > 19 or (x, y) in board
               for x, y in cells(shape, at))
assert not collision(orientation_0, origin)
kicks = [(0, 0), (-1, 0), (-1, -1), (0, 2), (-1, 2)]
attempts = [(origin[0] + dx, origin[1] + dy) for dx, dy in kicks]
assert attempts == [tuple(test["origin"]) for test in fixture["kick_tests"]]
assert [collision(orientation_r, at) for at in attempts] == [True, True, True, True, False]
locked = cells(orientation_r, attempts[4])
assert locked == {tuple(cell) for cell in fixture["expected_after_rotation"]["cells"]}
assert sum((x, y) in board for x, y in [(3, 17), (5, 17), (3, 19), (5, 19)]) == 4
assert collision(orientation_r, (3, 18))
merged = board | locked
assert [y for y in range(-20, 20) if all((x, y) in merged for x in range(10))] == [17, 18, 19]
assert fixture["expected_lock"]["clear_rows"] == [17, 18, 19]
print("Authored clockwise kick-4 T-spin triple geometry fixture passed")
