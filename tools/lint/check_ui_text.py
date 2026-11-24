"""#45 t08 lint: no new English literals in the swept HUD and menu code (player-visible text comes from MasterTextFile keys).

    python tools/lint/check_ui_text.py [--engine DIR] [--quiet]
    python tools/lint/check_ui_text.py --selftest

Scans the files in SCOPE (relative to engine/; each slice of the #45 sweep adds its files) for string literals that read
as English words: a word of 2+ letters followed by a space and another word, or a capitalised word ("Begin"), or words
ending in sentence punctuation. Ids (TEXT_*, IDC_*, snake_case, camelCase), paths, file names and format specs are not
English. Comments are not code and are never flagged. Exit 1 on any violation or malformed marker; 0 otherwise.

Allowed without a marker:
    a literal inside the arguments of text_or( / text_or_port( / txt( / caption_text( - a retail key's English stand-in
    a literal inside the arguments of port_text(                                - the call is the port's mark
    a literal inside the arguments of a log call (printf / fprintf / log:: / LOG_ / notes_.push_back, the script's ScriptControl notes.push_back) - diagnostics
    a literal in the same ( ) / { } / [ ] group as a "TEXT_*" key literal      - the English stand-in of that key
    ("inside the arguments" means between the call's parentheses at any depth, so in save_message(port_text(U"..."),
    "a detail") the detail is flagged (#308); the call may open on an earlier line of the literal's statement, back to
    the previous ';' / '{' / '}', at most 8 lines. Per literal by position: another copy of the same text elsewhere in
    the statement does not count.)
Allowed uses with a marker in a comment (the reason is required):
    [port-text: reason]        on the line of the literal, or in the comment lines directly above the statement
    [port-text-begin: reason]  ... [port-text-end]   every literal between the two lines
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_mod_names import allowed_lines, tokenize  # noqa: E402  (same tokenizer and marker rules)

SCOPE = [
    'ui/src/galactic_hud.cpp',  # #45 t08 slice 1
    'ui/src/galactic_hud_map.cpp',
    'ui/src/gui_menus.cpp',  # slice 2 (hotkey labels) and slice 3 (options and mods menus)
    'ui/include/alamo/ui/gui_menus.hpp',
    'apps/game/app/app_screens.cpp',  # slice 4 (the app's screens and messages)
    'apps/game/app/game_main.cpp',
]
MARK = re.compile(r'\[port-text(-begin)?:\s*([^\]]*)\]')
MARK_END = re.compile(r'\[port-text-end\]')
WORD = r"[A-Za-z][a-z]+(?:'[a-z]+)?"
ENGLISH = [
    re.compile(r'\b' + WORD + r'\s+[A-Za-z]{2,}'),          # two words: "Page Up", "Ships lost"
    re.compile(r'^\s*[A-Z][a-z]{2,}\s*[:.!?]?\s*$'),        # one capitalised word: "Begin", "Objectives:"
    re.compile(r'[A-Za-z]{2,}[.!?]\s*$'),                    # a sentence end: "Failed."
]
NOT_TEXT = [
    re.compile(r'^[A-Za-z0-9_]*_[A-Za-z0-9_]*$'),           # snake_case / TEXT_ ids
    re.compile(r'^[a-z]+[A-Z][A-Za-z0-9]*$'),               # camelCase
    re.compile(r'[\\/]|\.(?:xml|dat|tga|dds|alo|ala|meg|txt|lua|wav|mp3|bik)\b', re.I),  # paths, file names
]
# The code just before a '(' that opens an allowed call: a lookup, port_text, or a log line (#308: per call, not per
# statement).
ALLOWED_CALL = re.compile(r'(?:\b(?:text_or|text_or_port|txt|caption_text|port_text)|\b(?:f?printf|log::\w+|LOG_\w+)'
                          r'|\b(?:notes?_|ctl_\.notes|control\(\)\.notes)\.push_back)\s*$')  # diagnostics notes (App::ctl_ / AppProbe::control())
KEY_LITERAL = re.compile(r'TEXT_[A-Z0-9_]+')  # a key literal's content: the English beside it is its stand-in
RAW_OPEN = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(')  # as check_mod_names.tokenize


def is_english(content):
    s = re.sub(r'\\[nrt]', ' ', content).strip()  # a \n escape in the source is no path separator
    if len(s) < 3 or any(p.search(s) for p in NOT_TEXT):
        return False
    return any(p.search(s) for p in ENGLISH)


def statement_window(line, code_lines, src):
    """The lines of the statement holding `line`, in order: the line and the code lines above it back to the end of the
    previous statement (a line ending in ';', '{' or '}'), at most 8, so a printf( or text_or( call whose literal
    argument sits on a continuation line still counts."""
    lines = [line]
    for ln in sorted((l for l in code_lines if l < line), reverse=True):
        prev = src[ln - 1].split('//')[0].rstrip() if ln - 1 < len(src) else ''
        if prev.endswith((';', '{', '}')) or len(lines) >= 8:
            break
        lines.append(ln)
    return [ln for ln in sorted(lines) if ln - 1 < len(src)]


def statement_literals(lines, src):
    """The string literals of the statement on `lines`, in source order, as (line, content, in_call, group): in_call when
    an open parenthesis around it belongs to an allowed call (ALLOWED_CALL), group = the innermost open ( / { / [ (0: none).
    Comments (// and /* */ within a line) are skipped; raw strings read as check_mod_names.tokenize reads them."""
    out, stack, groups, in_block = [], [], 0, False
    for ln in lines:
        text = src[ln - 1]
        i = 0
        while i < len(text):
            if in_block:
                j = text.find('*/', i)
                if j < 0:
                    break
                in_block, i = False, j + 2
                continue
            if text.startswith('//', i):
                break
            if text.startswith('/*', i):
                in_block, i = True, i + 2
                continue
            c = text[i]
            here = (any(a for a, _ in stack), stack[-1][1] if stack else 0)
            m = RAW_OPEN.match(text, i)
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
                close = ')' + m.group(1) + '"'
                j = text.find(close, m.end())
                out.append((ln, text[m.end():j if j >= 0 else len(text)]) + here)  # a multi-line one: its first line
                i = j + len(close) if j >= 0 else len(text)
                continue
            if c == "'" and i > 0 and text[i - 1].isdigit():  # a digit separator (1'000), not a character literal
                i += 1
                continue
            if c in '"\'':
                j, buf = i + 1, []
                while j < len(text) and text[j] != c:
                    if text[j] == '\\' and j + 1 < len(text):
                        buf.append(text[j:j + 2])
                        j += 2
                        continue
                    buf.append(text[j])
                    j += 1
                if c == '"':
                    out.append((ln, ''.join(buf)) + here)
                i = j + 1
                continue
            if c in '([{':
                groups += 1
                stack.append((c == '(' and bool(ALLOWED_CALL.search(text[:i])), groups))
            elif c in ')]}' and stack:
                stack.pop()
            i += 1
    return out


def in_allowed_call(line, content, occurrence, code_lines, src):
    """Whether the `occurrence`-th literal `content` on `line` (0: the first) is allowed by its statement: it sits inside
    the parentheses of an allowed call, or in the same bracket group as a "TEXT_*" key literal."""
    lits = statement_literals(statement_window(line, code_lines, src), src)
    key_groups = {g for _, s, _, g in lits if KEY_LITERAL.fullmatch(s)}
    mine = [(in_call, g) for ln, s, in_call, g in lits if ln == line and s == content]
    if occurrence >= len(mine):
        return False
    in_call, group = mine[occurrence]
    return in_call or group in key_groups


def check_file(path, rel, out):
    text = path.read_text(encoding='utf8', errors='replace')
    src = text.split('\n')
    literals, comments, code_lines = tokenize(text)
    allowed, errors = allowed_lines(comments, code_lines, text.count('\n') + 1, src, MARK, MARK_END, 'port-text')
    bad = ok = 0
    for line, msg in errors:
        out.append(f'MARKER {rel}:{line}: {msg}')
        bad += 1
    seen = {}  # (line, content) -> copies so far: the literal's occurrence on its line
    for line, content in literals:
        occurrence = seen.get((line, content), 0)
        seen[(line, content)] = occurrence + 1
        if not is_english(content):
            continue
        if line in allowed or in_allowed_call(line, content, occurrence, code_lines, src):
            ok += 1
            continue
        shown = content if len(content) <= 60 else content[:57] + '...'
        out.append(f'VIOLATION {rel}:{line}: "{shown}" -> a MasterTextFile key (text_or / txt), port_text(), or a '
                   '[port-text: reason] marker')
        bad += 1
    return bad, ok


def scan(engine, scope=SCOPE):
    files = [engine / rel for rel in scope if (engine / rel).is_file()]
    missing = [rel for rel in scope if not (engine / rel).is_file()]
    out, bad, ok = [], 0, 0
    for rel in missing:
        out.append(f'SCOPE {rel}: not found')
        bad += 1
    for p in files:
        b, o = check_file(p, p.relative_to(engine).as_posix(), out)
        bad += b
        ok += o
    return files, out, bad, ok


def run(engine, quiet=False):
    files, out, bad, ok = scan(Path(engine))
    for line in out:
        print(line)
    if bad:
        print(f'check_ui_text: {bad} finding(s) in {len(files)} files (#45: MasterTextFile keys, or port_text / [port-text])')
        return 1
    if not quiet:
        print(f'check_ui_text: OK ({len(files)} files, {ok} literals allowed by a lookup call or a marker)')
    return 0


def selftest(here):
    """Unit checks of the English rule, then the fixture files under tools/lint/fixture_ui_text."""
    fails = []

    def expect(cond, what):
        if not cond:
            fails.append(what)

    for s in ['Page Up', 'Begin', 'Objectives:', 'Failed to save game.', "You've won", 'Ships lost: ']:
        expect(is_english(s), f'English: {s!r}')
    for s in ['TEXT_BUTTON_BEGIN', 'IDC_PORT_MODS_LIST', 'hud_text', 'fleetPane', 'Data\\XML\\GameConstants.xml',
              '%d', ' ', 'x', 'F%d', '##1', 'MSAA 4x', 'Default.tga']:
        expect(not is_english(s), f'not English: {s!r}')
    fixture = here / 'fixture_ui_text'
    files, out, bad, ok = scan(fixture, ['ui/src/menu.cpp'])
    got = sorted(o.split(':')[0] + ':' + o.split(':')[1] for o in out)
    want = sorted(['VIOLATION ui/src/menu.cpp:4', 'VIOLATION ui/src/menu.cpp:5', 'MARKER ui/src/menu.cpp:13',
                   'VIOLATION ui/src/menu.cpp:16', 'VIOLATION ui/src/menu.cpp:23',
                   'VIOLATION ui/src/menu.cpp:25', 'VIOLATION ui/src/menu.cpp:26', 'VIOLATION ui/src/menu.cpp:27',
                   'VIOLATION ui/src/menu.cpp:30'])
    expect(got == want, 'fixture findings:\n  got  ' + '\n       '.join(got) + '\n  want ' + '\n       '.join(want))
    expect(ok == 14, f'fixture: {ok} literals allowed, want 14')
    _, out2, bad2, _ = scan(fixture, ['ui/src/missing.cpp'])
    expect(bad2 == 1 and out2 and out2[0].startswith('SCOPE'), 'a missing scope file is a finding')
    for f in fails:
        print('SELFTEST FAIL', f)
    if fails:
        return 1
    print(f'check_ui_text selftest: OK ({len(out)} expected findings, {ok} allowed)')
    return 0


def main(argv):
    here = Path(__file__).resolve().parent
    engine = here.parent.parent / 'engine'
    quiet = False
    args = list(argv)
    if '--selftest' in args:
        return selftest(here)
    while args:
        a = args.pop(0)
        if a == '--engine' and args:
            engine = Path(args.pop(0))
        elif a == '--quiet':
            quiet = True
        else:
            print(__doc__)
            return 2
    return run(engine, quiet)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
