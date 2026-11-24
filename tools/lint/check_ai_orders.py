"""#147 AI order lint: the tactical AI (BattleAi) issues no combat or movement order itself.

    python tools/lint/check_ai_orders.py [--root DIR]
    python tools/lint/check_ai_orders.py --selftest

Every AI order enters the battle through TacticalBattle::apply_order (BattleAi::execute_order passes the AI as the
issuer; the AI's order rules are TacticalBattle::ai_order) or through a TacticalBattle function: the follow re-paths and
the guard drop (ai_follow_move, ai_guard_drop), the guard follow's re-plan (land_attack_order), and the Lua Divert hook's
move (ai_move_unit + fighter_order).

Rule: in engine/apps/sim/src/battle_ai*.cpp, a call of an order function of the combat or movement system - through the
battle's members (combat_-> / movement_->) or through a local alias of them (`X& c = *b_->combat_;` ... `c.f(`) - is a
finding. Order functions: CombatSystem order_attack, order_attack_issue, order_fire_at_position, clear_order,
clear_targets, set_attack_goal; MovementSystem move, stop, halt, stop_and_hold, stop_and_leave, teleport, set_heading,
set_attack_target, clear_attack_block, fighter_new_order, fighter_push, fighter_pop, set_fighter_target,
fighter_dead_stop. Reads (has_unit, view, is_enemy, ...) and the task-force release writes (priority override,
stickiness, Active_By_Default) are not orders. Comments and string literals are ignored. Exit 1 on a finding, else 0.
"""
import re
import sys
from pathlib import Path

COMBAT_ORDERS = ("order_attack_issue", "order_attack", "order_fire_at_position", "clear_order", "clear_targets",
                 "set_attack_goal")
MOVEMENT_ORDERS = ("move", "stop_and_hold", "stop_and_leave", "stop", "halt", "teleport", "set_heading", "set_attack_target",
                   "clear_attack_block", "fighter_new_order", "fighter_push", "fighter_pop", "set_fighter_target",
                   "fighter_dead_stop")
SYSTEMS = {"combat_": COMBAT_ORDERS, "movement_": MOVEMENT_ORDERS}
ALIAS = re.compile(r"\b(\w+)\s*=\s*\*\s*(?:\w+->)?(combat_|movement_)\s*;")
COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)


def strip(text):
    # keep the line count: a comment or string becomes spaces (its newlines kept)
    return COMMENT_OR_STRING.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def order_call(receivers):
    """`receiver f(` for the order functions of each receiver's system; receivers: {regex: system}"""
    parts = [rf"{rx}(?:{'|'.join(SYSTEMS[system])})" for rx, system in receivers.items()]
    return re.compile(r"(?:" + "|".join(parts) + r")\s*\(")


def findings(name, text):
    code = strip(text)
    receivers = {r"\bcombat_->": "combat_", r"\bmovement_->": "movement_"}
    for m in ALIAS.finditer(code):  # a local alias of a system (`tc::CombatSystem& cs = *b_->combat_;`)
        alias = re.escape(m.group(1))
        receivers[rf"\b{alias}\."] = m.group(2)
        receivers[rf"\b{alias}->"] = m.group(2)
    call = order_call(receivers)
    out = []
    for i, line in enumerate(code.splitlines(), 1):
        m = call.search(line)
        if m:
            out.append(f"{name}:{i}: the AI issues an order directly ({m.group(0).rstrip('( ')}); "
                       "route it through TacticalBattle::apply_order / ai_order (#147)")
    return out


def selftest():
    good = "\n".join([
        "b_->apply_order(*mode_, p, ev, {}, rng, this);",
        "if (b_->combat_->has_unit(id)) {}",
        "// combat_->order_attack(a, b)",
        "tc::CombatSystem& cs = *b_->combat_;",
        "cs.reset_stickiness(id);",
        "b_->movement_->view(id);",
        "",
    ])
    bad = "\n".join([
        "x();",
        "  b_->combat_->order_attack_issue(u, t, -1);",
        'const char* s = "movement_->move(";',
        "b_->movement_->move(o, r, f);",
        "auto& m = *b_->movement_;",
        "m.teleport(id, p);",
        "tc::CombatSystem& cs = *b_->combat_;",
        "cs.clear_targets(id);",
        "b_->movement_->stop_and_hold(ids);",
        "",
    ])
    assert findings("good", good) == [], findings("good", good)
    got = findings("bad", bad)
    assert [g.split(":")[1] for g in got] == ["2", "4", "6", "8", "9"], got
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
