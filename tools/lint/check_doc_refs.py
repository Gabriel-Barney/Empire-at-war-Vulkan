"""Doc-reference lint: every cited Markdown document exists in this repository or is a listed legacy document.

    python tools/lint/check_doc_refs.py [--root DIR] [--exclude PATH]... [--quiet]
    python tools/lint/check_doc_refs.py --selftest

Scans every tracked text file under --root, and every untracked, not ignored one inside a folder that holds tracked
files (git ls-files; a plain walk when DIR is not a git work tree), except generated and third-party trees (SKIP_DIRS) and the files in SKIP_FILES. A file or folder
named by --exclude (repeatable, a path relative to the root) is not scanned either. For each citation of a Markdown
document (docs/LEGACY_DOCS.md, "Reference convention"):

  legacy <path>       <path> must be listed in docs/LEGACY_DOCS.md (a permalink into the old checkout).
  docs/<...>.md       and any other path whose first folder is a top-level folder of the repo: the file must exist.
                      A listed legacy document cited without the `legacy` prefix is reported with the fix.
  NAME.md             a bare upper-case name: fine when some document of the repo has that name; reported when only
                      a legacy document has it (cite `legacy <full path>`); other names are not checked.
  docs/MODS.md "H"    for the authoritative documents (AUTHORITATIVE), a quoted heading right after the path must be
                      one of the document's headings, and a numbered section (`§5.1`, `section 3`, `5.1`) is reported:
                      their headings are not numbered, so a number is an old-document citation.

A line that carries the marker `doc-ref-ok` is not checked (for literal fixtures that look like citations).
Exit 1 on any finding, 0 otherwise.
"""
import os
import re
import subprocess
import sys
import tempfile

BS = chr(92)
LEGACY_INDEX = 'docs/LEGACY_DOCS.md'
AUTHORITATIVE = ('docs/MODS.md', 'docs/SIMULATION.md', 'docs/DECISIONS.md')
SKIP_DIRS = ('build/', 'launcher/target/', 'toolchain/', 'extracted/', 'logs/', '.git/')
# Files whose .md names are literal test data, not citations.
SKIP_FILES = {
    'tools/lint/check_tag_ledger.py',  # self-test fixtures: made-up docs/re pages (A.md, X.md, AAA_NEW.md, ...)
    'tools/lint/fixture_mod_names/galactic/src/bad.cpp',  # fixture of check_mod_names.py (a made-up docs/re page)
    'tools/lint/check_doc_refs.py',  # this lint: its doc string and self-test name made-up documents
}
TEXT_EXT = {'.cpp', '.hpp', '.h', '.inl', '.cc', '.c', '.hlsl', '.hlsli', '.glsl', '.rs', '.py', '.cmake', '.txt', '.md',
            '.json', '.cmd', '.bat', '.ps1', '.sh', '.toml', '.yml', '.yaml', '.inc', '.fx', '.fxh', '.ini', '.cfg',
            '.xml', '.html', '.js', '.ts'}
MARKER = 'doc-ref-ok'

REF = re.compile(r'(?<![A-Za-z0-9_./' + BS + BS + r'-])((?:[A-Za-z0-9_.' + BS + BS + r'-]+[/' + BS + BS +
                 r'])*[A-Za-z0-9_-][A-Za-z0-9_.-]*\.md)(?![A-Za-z0-9_])')
BARE_CAPS = re.compile(r'[A-Z0-9][A-Z0-9_]*\.md')
LEGACY_ROW = re.compile(r'\]\(https://github\.com/[^/\s]+/[^/\s]+/blob/[0-9a-f]{40}/([^)\s]+\.md)\)')
QT = r'(?:' + BS + BS + r'"([^"' + BS + BS + r']{1,100})' + BS + BS + r'"|"([^"\n]{1,100})"|\'([^\'\n]{1,100})\')'
HEADINGS_AFTER = re.compile(r'`?,?[ \t]+' + QT + r'((?:,[ \t]+' + QT + r')*)')
NUMBERED_AFTER = re.compile(r'`?,?[ \t]+(?:§[ \t]?\d|sections?[ \t]+\d|\d+\.\d|\d+(?:[ \t]|,|\)|$))')


def list_files(root):
    """Tracked files, plus untracked (not ignored) files inside the folders that hold tracked files, so a new document
    or a new citation is checked before it is committed while unpacked third-party trees at the root are not."""
    def git(*args):
        return subprocess.check_output(['git', '-C', root, *args], text=True, stderr=subprocess.DEVNULL)
    try:
        if git('rev-parse', '--show-toplevel').strip().replace(BS, '/').rstrip('/').lower() == \
                os.path.abspath(root).replace(BS, '/').rstrip('/').lower():
            tracked = {f for f in git('ls-files', '--cached').split('\n') if f}
            dirs = {f.split('/', 1)[0] for f in tracked if '/' in f}
            others = {f for f in git('ls-files', '--others', '--exclude-standard', '--', *sorted(dirs)).split('\n')
                      if f} if dirs else set()
            return sorted(tracked | others)
    except (OSError, subprocess.CalledProcessError):
        pass
    files = []
    for d, dirs, names in os.walk(root):
        dirs[:] = [x for x in dirs if x != '.git']
        for n in names:
            files.append(os.path.relpath(os.path.join(d, n), root).replace(BS, '/'))
    return sorted(files)


def headings(path):
    out = set()
    try:
        with open(path, encoding='utf-8') as f:
            for line in f:
                m = re.match(r'#{1,6}\s+(.*?)\s*#*\s*$', line)
                if m:
                    out.add(m.group(1))
    except OSError:
        pass
    return out


def check(root, excludes=(), quiet=False):
    root = os.path.abspath(root)
    files = list_files(root)
    present = set(files) | {f for f in files if os.path.isfile(os.path.join(root, f))}
    top = {f.split('/', 1)[0] for f in files if '/' in f}
    md_names = {os.path.basename(f) for f in present if f.endswith('.md')}
    legacy = set()
    index = os.path.join(root, LEGACY_INDEX)
    if os.path.isfile(index):
        legacy = set(LEGACY_ROW.findall(open(index, encoding='utf-8').read()))
    legacy_by_name = {}
    for p in legacy:
        legacy_by_name.setdefault(os.path.basename(p), []).append(p)
    heads = {d: headings(os.path.join(root, d)) for d in AUTHORITATIVE}
    excl = [e.replace(BS, '/').strip('/') for e in excludes]
    findings = []
    scanned = 0
    for f in files:
        if f.startswith(SKIP_DIRS) or f in SKIP_FILES or f == LEGACY_INDEX:
            continue
        if any(f == e or f.startswith(e + '/') for e in excl):
            continue
        if os.path.splitext(f)[1].lower() not in TEXT_EXT and os.path.basename(f) not in ('CMakeLists.txt',):
            continue
        try:
            with open(os.path.join(root, f), encoding='utf-8') as fh:
                text = fh.read()
        except (UnicodeDecodeError, OSError):
            continue
        scanned += 1
        for n, line in enumerate(text.split('\n'), 1):
            if '.md' not in line or MARKER in line:
                continue
            for m in REF.finditer(line):
                raw = m.group(1)
                path = raw.replace(BS, '/')
                while path.startswith('./'):
                    path = path[2:]
                s, e = m.span(1)
                before = line[max(0, s - 8):s]
                where = f'{f}:{n}'
                if before.endswith('legacy ') or before.endswith('legacy `'):
                    if path not in legacy:
                        findings.append(f'{where}: legacy {path} is not listed in {LEGACY_INDEX}')
                    continue
                if '/' in path:
                    if path.startswith(SKIP_DIRS):
                        continue  # generated output (build/tag-ledger.md): exists only after a build, never in a checkout
                    if path.split('/', 1)[0] not in top:
                        continue  # a path outside this tree (third-party, build output, relative)
                    if path in present:
                        if path in heads:
                            findings += check_headings(where, path, line[e:], heads[path])
                        continue
                    if path in legacy:
                        findings.append(f'{where}: {path} is a legacy document: cite it as `legacy {path}`')
                    else:
                        findings.append(f'{where}: {path} does not exist and is not listed in {LEGACY_INDEX}')
                    continue
                if not BARE_CAPS.fullmatch(path) or line[max(0, s - 1):s] == '<':
                    continue
                if path in md_names:
                    full = 'docs/' + path
                    if full in heads:
                        findings += check_headings(where, full, line[e:], heads[full])
                    continue
                if path in legacy_by_name:
                    findings.append(f'{where}: bare {path}: cite `legacy {" or ".join(sorted(legacy_by_name[path]))}`')
    if not quiet:
        for x in findings:
            print(x)
        print(f'check_doc_refs: {len(findings)} finding(s) in {scanned} files ({len(legacy)} legacy documents listed)')
    return findings


def check_headings(where, doc, after, heads):
    m = HEADINGS_AFTER.match(after)
    if m:
        cited = [next(g for g in m.groups()[0:3] if g is not None)]
        for q in re.finditer(QT, m.group(4) or ''):
            cited.append(next(g for g in q.groups() if g is not None))
        return [f'{where}: {doc} has no heading "{h}"' for h in cited if h not in heads]
    if NUMBERED_AFTER.match(after):
        return [f'{where}: {doc} is cited with a section number; its headings are not numbered (cite "Heading")']
    return []


def selftest():
    cases = {
        'docs/LEGACY_DOCS.md': '| [docs/re/OLD.md](https://github.com/o/r/blob/' + '0' * 40 + '/docs/re/OLD.md) |\n',
        'docs/MODS.md': '# Mods\n\n## File resolution order\n',
        'docs/re/HERE.md': '# Here\n',
        'engine/ok.cpp': ('// legacy docs/re/OLD.md §3, docs/re/HERE.md, HERE.md, docs/MODS.md "File resolution order"\n'
                          '// `docs/MODS.md` "File resolution order", vendor/docs/license.md, build/x.md, <CATALOG.md>\n'
                          '// legacy `docs/re/OLD.md` 2.1; README.md; docs/MODS.md, then docs/re/OLD.md doc-ref-ok\n'),
        'engine/bad.cpp': ('// docs/re/OLD.md\n'                               # 1: legacy without prefix
                           '// OLD.md\n'                                       # 2: bare legacy name
                           '// legacy docs/re/GONE.md\n'                       # 3: legacy not listed
                           '// docs/NOPE.md\n'                                 # 4: missing
                           '// docs/MODS.md §3.2\n'                            # 5: numbered section
                           '// docs/MODS.md "No such heading"\n'               # 6: unknown heading
                           '// docs/MODS.md "File resolution order", "Nope"\n'),  # 7: second heading unknown
        'README.md': 'x\n',
    }
    want = {'engine/bad.cpp:1', 'engine/bad.cpp:2', 'engine/bad.cpp:3', 'engine/bad.cpp:4', 'engine/bad.cpp:5',
            'engine/bad.cpp:6', 'engine/bad.cpp:7'}
    with tempfile.TemporaryDirectory() as d:
        for rel, text in cases.items():
            p = os.path.join(d, *rel.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'w', encoding='utf-8') as f:
                f.write(text)
        got = check(d, quiet=True)
    where = {':'.join(x.split(':', 2)[:2]) for x in got}
    ok = where == want and len(got) == len(want)
    if not ok:
        print('check_doc_refs selftest FAIL')
        for x in got:
            print('  got', x)
        print('  want', sorted(want))
        return 1
    print(f'check_doc_refs selftest: {len(want)} findings as expected, clean file clean')
    return 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    excludes, quiet = [], False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--root' and i + 1 < len(argv):
            root = argv[i + 1]
            i += 1
        elif a == '--exclude' and i + 1 < len(argv):
            excludes.append(argv[i + 1])
            i += 1
        elif a == '--quiet':
            quiet = True
        else:
            print(__doc__)
            return 2
        i += 1
    return 1 if check(root, excludes, quiet) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
