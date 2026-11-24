"""#147 AI order lint: the tactical AI (BattleAi) issues no combat or movement order itself.

    python tools/lint/check_ai_orders.py [--root DIR]
    python tools/lint/check_ai_orders.py --selftest

Every AI order enters the battle through TacticalBattle::apply_order (BattleAi::execute_order passes the AI as the
issuer; the AI's order rules are TacticalBattle::ai_order) or through a TacticalBattle function (the follow re-paths:
ai_follow_move, ai_guard_drop, ai_move_unit). Rule: in engine/apps/sim/src/battle_ai*.cpp, a call of an order function of
the combat or movement system through the battle's members - combat_->order_attack / order_attack_issue / clear_order /
set_attack_goal, movement_->move / stop / fighter_new_order / clear_attack_block - is a finding. Reads (has_unit, view,
is_enemy, ...) are not orders. Comments and string literals are ignored. Exit 1 on a finding, else 0.
"""
import re
import sys
from pathlib import Path

ORDER_CALL = re.compile(
    r"\b(?:combat_->(?:order_attack_issue|order_attack|clear_order|set_attack_goal)"
    r"|movement_->(?:move|stop|fighter_new_order|clear_attack_block))\s*\("
)
COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)


def strip(text):
    # keep the line count: a comment or string becomes spaces (its newlines kept)
    return COMMENT_OR_STRING.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def findings(name, text):
    out = []
    for i, line in enumerate(strip(text).splitlines(), 1):
        m = ORDER_CALL.search(line)
        if m:
            out.append(f"{name}:{i}: the AI issues an order directly ({m.group(0).rstrip('( ')}); "
                       "route it through TacticalBattle::apply_order / ai_order (#147)")
    return out


def selftest():
    good = "b_->apply_order(*mode_, p, ev, {}, rng, this);\nif (b_->combat_->has_unit(id)) {}\n// combat_->order_attack(a, b)\n"
    bad = 'x();\n  b_->combat_->order_attack_issue(u, t, -1);\nconst char* s = "movement_->move(";\nb_->movement_->move(o, r, f);\n'
    assert findings("good", good) == [], findings("good", good)
    got = findings("bad", bad)
    assert [g.split(":")[1] for g in got] == ["2", "4"], got
    print("check_ai_orders selftest: ok")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[argv.index("--root") + 1]) if "--root" in argv else Path(__file__).resolve().parents[2]
    files = sorted((root / "engine/apps/sim/src").glob("battle_ai*.cpp"))
    if not files:
        print("check_ai_orders: no engine/apps/sim/src/battle_ai*.cpp under", root)
        return 1
    problems = []
    for f in files:
        problems += findings(f.relative_to(root).as_posix(), f.read_text(encoding="utf-8"))
    for p in problems:
        print(p)
    print(f"check_ai_orders: {len(problems)} finding(s) in {len(files)} file(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
