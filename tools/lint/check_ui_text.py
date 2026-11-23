"""#45 t08 lint: no new English literals in the swept HUD and menu code (player-visible text comes from MasterTextFile keys).

    python tools/lint/check_ui_text.py [--engine DIR] [--quiet]
    python tools/lint/check_ui_text.py --selftest

Scans the files in SCOPE (relative to engine/; each slice of the #45 sweep adds its files) for string literals that read
as English words: a word of 2+ letters followed by a space and another word, or a capitalised word ("Begin"), or words
ending in sentence punctuation. Ids (TEXT_*, IDC_*, snake_case, camelCase), paths, file names and format specs are not
English. Comments are not code and are never flagged. Exit 1 on any violation or malformed marker; 0 otherwise.

Allowed without a marker:
    a literal on a line that calls text_or( / text_or_port( / txt( / caption_text(, or that holds a "TEXT_*" key
                                                                                - the English stand-in of a retail key
    a literal on a line that calls port_text(                                   - the call is the port's mark
    a literal on a line that logs (printf / fprintf / log:: / LOG_ / notes_.push_back) - diagnostics, not UI text
    ("on a line" means anywhere in the literal's statement, back to the previous ';' / '{' / '}', at most 8 lines)
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
ALLOWED_CALL = re.compile(r'\b(?:text_or|text_or_port|txt|caption_text|port_text)\s*\(|\b(?:f?printf|log::\w+|LOG_\w+)\s*\('
                          r'|\bnotes?_\.push_back\s*\('  # diagnostics notes
                          r'|"TEXT_[A-Z0-9_]+"')  # a key literal on the line: the English is its stand-in


def is_english(content):
    s = re.sub(r'\\[nrt]', ' ', content).strip()  # a \n escape in the source is no path separator
    if len(s) < 3 or any(p.search(s) for p in NOT_TEXT):
        return False
    return any(p.search(s) for p in ENGLISH)


def statement_code(line, code_lines, src):
    """The code of the statement holding `line`: the line and the code lines above it back to the end of the previous
    statement (a line ending in ';', '{' or '}'), at most 8, so a printf( or text_or( call whose literal argument sits on
    a continuation line still counts."""
    lines = [line]
    for ln in sorted((l for l in code_lines if l < line), reverse=True):
        prev = src[ln - 1].split('//')[0].rstrip() if ln - 1 < len(src) else ''
        if prev.endswith((';', '{', '}')) or len(lines) >= 8:
            break
        lines.append(ln)
    return '\n'.join(src[ln - 1] for ln in sorted(lines) if ln - 1 < len(src))


def check_file(path, rel, out):
    text = path.read_text(encoding='utf8', errors='replace')
    src = text.split('\n')
    literals, comments, code_lines = tokenize(text)
    allowed, errors = allowed_lines(comments, code_lines, text.count('\n') + 1, src, MARK, MARK_END, 'port-text')
    bad = ok = 0
    for line, msg in errors:
        out.append(f'MARKER {rel}:{line}: {msg}')
        bad += 1
    for line, content in literals:
        if not is_english(content):
            continue
        if line in allowed or ALLOWED_CALL.search(statement_code(line, code_lines, src)):
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
                   'VIOLATION ui/src/menu.cpp:16'])
    expect(got == want, 'fixture findings:\n  got  ' + '\n       '.join(got) + '\n  want ' + '\n       '.join(want))
    expect(ok == 7, f'fixture: {ok} literals allowed, want 7')
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
