#!/usr/bin/env python3
"""Changelog fragments (tasks 0443 and 0465, ADR 0123). A round never edits CHANGELOG.md, README.md or the shared
docs (docs/USABILITY.md, tools/README.md, docs/ARCHITECTURE.md, docs/PERFORMANCE.md): it adds
changelog.d/<round>-<slug>.md, its CHANGELOG entry plus optional sections of edits to those files, and the pass
write-up folds them in. New files never conflict, so the PRs of passes running in parallel merge in any order.

Usage: py tools/dev/changelog.py check                  every fragment well formed, each edit applicable exactly once
       py tools/dev/changelog.py fold [--checks LIBS TESTS] [--dry-run]
                                                        fragments into CHANGELOG.md, README.md and the shared docs,
                                                        then deleted
       py tools/dev/changelog.py same-render OLD NEW    two READMEs read the same once soft line breaks are joined
       py tools/dev/changelog.py --self-test            the task's acceptance cases, in temporary folders only
--root DIR picks the tree (default: the one this script is in). The format is in changelog.d/README.md."""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fill_checks import readme_count  # noqa: E402  (one count rule for the chain and the fold)

TRAILER = 'Docs-fold: yes'
README_MARK = '<!-- README -->'
# The files a fragment may edit, by the path its section mark names: `<!-- docs/USABILITY.md -->`. README keeps its
# short mark. tools/dev/docs_check.sh holds the same list (SHARED_DOCS; the self-test compares them).
SHARED_DOCS = ('docs/USABILITY.md', 'tools/README.md', 'docs/ARCHITECTURE.md', 'docs/PERFORMANCE.md')
TARGETS = ('README.md',) + SHARED_DOCS
MARKS = dict({'README': 'README.md'}, **{t: t for t in SHARED_DOCS})
MARK = re.compile(r'^<!-- (\S+) -->[ \t]*$')
NAME = re.compile(r'^(\d+)-[A-Za-z0-9._-]+\.md$')
HEADING = re.compile(r'^## \d{4}-\d{2}-\d{2} \(round (\d+)\): \S')
OP = re.compile(r'^(after|replace|with|append):[ \t]?(.*)$')


class FoldError(Exception):
    pass


def read(path):
    with open(path, encoding='utf-8', newline='') as f:
        raw = f.read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    return raw.replace('\r\n', '\n'), nl


def write(path, text, nl):
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text.replace('\n', nl))


def fragment_paths(root):
    d = os.path.join(root, 'changelog.d')
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, n) for n in os.listdir(d) if NAME.match(n))


def op_body(lines, i):
    """The lines below operation line i, up to the next operation, without the blank lines around them."""
    j = i + 1
    while j < len(lines) and not OP.match(lines[j]):
        j += 1
    body = lines[i + 1:j]
    while body and not body[-1].strip():
        body.pop()
    while body and not body[0].strip():
        body.pop(0)
    return body, j


def parse_ops(name, target, lines):
    """A section's operations: `after: <line start>` + the lines to insert, `replace: <text>` + `with: <text>`, or
    `append:` + the block that goes at the file's end."""
    ops, i = [], 0
    where = '%s: %s section' % (name, target)
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        m = OP.match(line)
        if not m or m.group(1) == 'with':
            raise FoldError('%s: expected "after:", "replace:" or "append:", found %r' % (where, line[:60]))
        kind, text = m.group(1), m.group(2)
        if kind == 'append':
            if text.strip():
                raise FoldError('%s: "append:" stands alone on its line, the block goes on the lines below it' % where)
            body, i = op_body(lines, i)
            if not body:
                raise FoldError('%s: "append:" has no lines below it' % where)
            ops.append(('append', '', body))
            continue
        if not text.strip():
            raise FoldError('%s: "%s:" names no text' % (where, kind))
        if kind == 'replace':
            w = OP.match(lines[i + 1]) if i + 1 < len(lines) else None
            if not w or w.group(1) != 'with':
                raise FoldError('%s: "replace:" must be followed by "with:"' % where)
            ops.append(('replace', text, w.group(2)))
            i += 2
            continue
        body, i = op_body(lines, i)
        if not body:
            raise FoldError('%s: "after:" has no lines below it' % where)
        ops.append(('after', text, body))
    return ops


def split_sections(name, text):
    """The entry, then [(target file, its lines)] for each `<!-- README -->` / `<!-- path -->` section below it."""
    entry, sections, current = [], [], None
    for line in text.split('\n'):
        m = MARK.match(line)
        token = m.group(1) if m else None
        if token in MARKS:
            if any(t == MARKS[token] for t, _ in sections):
                raise FoldError('%s: two "<!-- %s -->" sections (a file has one)' % (name, token))
            current = []
            sections.append((MARKS[token], current))
            continue
        if token and token.endswith('.md'):
            raise FoldError('%s: "<!-- %s -->" names no file a fragment may edit (README, %s)' % (name, token, ', '.join(SHARED_DOCS)))
        (entry if current is None else current).append(line)
    return '\n'.join(entry), sections


def parse_fragment(path):
    name = os.path.basename(path)
    text, _ = read(path)
    entry, sections = split_sections(name, text)
    entry = entry.strip('\n')
    first = entry.split('\n', 1)[0]
    m = HEADING.match(first)
    if not m:
        raise FoldError('%s: the entry must start with "## YYYY-MM-DD (round N): title", found %r' % (name, first[:60]))
    rnd = int(m.group(1))
    if int(NAME.match(name).group(1)) != rnd:
        raise FoldError('%s: the file name says round %s, the heading round %d' % (name, NAME.match(name).group(1), rnd))
    return {'path': path, 'name': name, 'round': rnd, 'entry': entry,
            'sections': [(target, parse_ops(name, target, lines)) for target, lines in sections]}


def apply_ops(frag, target, ops, doc):
    """One fragment's operations on one file's text. Each must apply exactly once: an anchor found once, a replaced
    text found once, an appended block whose first line is not in the file yet (so a fold run twice, or two fragments
    with the same block, stop instead of doubling it)."""
    for kind, text, arg in ops:
        if kind == 'replace':
            n = doc.count(text)
            if n != 1:
                raise FoldError('%s: "replace:" text found %d times in %s (needs exactly 1): %r' % (frag['name'], n, target, text[:60]))
            doc = doc.replace(text, arg)
            continue
        lines = doc.split('\n')
        if kind == 'append':
            if arg[0] in lines:
                raise FoldError('%s: "append:" block is already in %s (its first line is there): %r' % (frag['name'], target, arg[0][:60]))
            while lines and not lines[-1].strip():
                lines.pop()
            doc = '\n'.join(lines + ([''] if lines else []) + arg + [''])
            continue
        # README's list items indent their continuation lines: the anchor ignores that indent, and inserted lines
        # written without one take the anchor's, so they stay in the item.
        hits = [k for k, l in enumerate(lines) if l.lstrip().startswith(text.strip())]
        if len(hits) != 1:
            raise FoldError('%s: "after:" matches %d %s lines (needs exactly 1): %r' % (frag['name'], len(hits), target, text[:60]))
        anchor = lines[hits[0]]
        indent = anchor[:len(anchor) - len(anchor.lstrip())]
        lines[hits[0] + 1:hits[0] + 1] = [l if l[:1] in (' ', '\t') or not l.strip() else indent + l for l in arg]
        doc = '\n'.join(lines)
    return doc


def insert_entries(changelog, entries):
    """Entries go right above the first `## ` heading (the newest entry), below the intro."""
    lines = changelog.split('\n')
    at = next((k for k, l in enumerate(lines) if l.startswith('## ')), None)
    block = []
    for e in entries:
        block += e.split('\n') + ['']
    if at is None:
        while lines and not lines[-1]:
            lines.pop()
        return '\n'.join(lines + [''] + block)
    return '\n'.join(lines[:at] + block + lines[at:])


def plan(root, checks=None):
    """Everything a fold would write, computed in memory; raises FoldError and writes nothing on any problem."""
    paths = fragment_paths(root)
    frags, errors = [], []
    for p in paths:
        try:
            frags.append(parse_fragment(p))
        except FoldError as e:
            errors.append(str(e))
    if errors:
        raise FoldError('\n'.join(errors))
    cl_path = os.path.join(root, 'CHANGELOG.md')
    changelog, cl_nl = read(cl_path)
    docs = {}  # target -> [path, old text, new text, newline]; README always (its count), the others when edited

    def doc(target, name):
        if target not in docs:
            path = os.path.join(root, *target.split('/'))
            if not os.path.isfile(path):
                raise FoldError('%s: a "<!-- %s -->" section, but the tree has no %s' % (name, target, target))
            text, nl = read(path)
            docs[target] = [path, text, text, nl]
        return docs[target]

    doc('README.md', 'changelog.py')
    for f in sorted(frags, key=lambda f: (f['round'], f['name'])):
        for target, ops in f['sections']:
            d = doc(target, f['name'])
            d[2] = apply_ops(f, target, ops, d[2])
    if checks:
        docs['README.md'][2] = readme_count(docs['README.md'][2], checks[0], checks[1])
    newest = sorted(frags, key=lambda f: (-f['round'], f['name']))
    new_changelog = insert_entries(changelog, [f['entry'] for f in newest]) if frags else changelog
    files = [('CHANGELOG.md', (cl_path, changelog, new_changelog, cl_nl))]
    files += [(t, tuple(docs[t])) for t in TARGETS if t in docs]
    return {'frags': frags, 'files': files}


def remove(root, path):
    r = subprocess.run(['git', '-C', root, 'rm', '-q', '-f', '--', path], capture_output=True)
    if r.returncode != 0 and os.path.exists(path):
        os.remove(path)


def swap(changed):
    """Writes [(path, old, new, nl)] all or nothing: each new text to a temporary first, then swapped in; if a swap
    fails (on Windows a file an editor holds open fails here, not at the temporary), the files already swapped get
    their old text back, so a second fold never inserts the entries twice."""
    done, failing = [], None
    try:
        for path, _, new, nl in changed:
            failing = path
            write(path + '.fold-tmp', new, nl)
        for path, _, _, _ in changed:
            failing = path
            os.replace(path + '.fold-tmp', path)
            done.append(path)
    except OSError as e:
        for path, old, _, nl in changed:
            if path in done:
                write(path, old, nl)
            if os.path.exists(path + '.fold-tmp'):
                os.remove(path + '.fold-tmp')
        raise FoldError('could not write %s (%s); every file is as it was' % (os.path.basename(failing), e))


def cmd_fold(root, checks, dry):
    p = plan(root, checks)
    if not p['frags'] and not checks:
        print('no fragments in changelog.d/ - nothing to fold')
        return 0
    for f in p['frags']:
        if re.search(r'\bCHECKS_(LIBS|W|L)\b', f['entry']):
            print('warning: changelog.d/%s still holds CHECKS_* placeholders (fill_checks.py was not run for it)' % f['name'])
    changed = [f for _, f in p['files'] if f[1] != f[2]]
    for label, (_, old, new, _) in p['files']:
        print('%s %s' % ('unchanged' if old == new else ('would change' if dry else 'changed'), label))
    if not dry:
        swap(changed)
    for f in p['frags']:
        print('%s changelog.d/%s (round %d)' % ('would fold' if dry else 'folded', f['name'], f['round']))
        if not dry:
            remove(root, f['path'])
    if not dry:
        print('Put this line in the commit message (tools/dev/docs_check.sh lets the commit edit CHANGELOG, README and the shared docs):')
        print('  ' + TRAILER)
    return 0


def cmd_check(root):
    p = plan(root)
    print('changelog.d: %d fragment(s), all well formed and applicable' % len(p['frags']))
    for target in TARGETS:
        n = sum(len(ops) for f in p['frags'] for t, ops in f['sections'] if t == target)
        if n:
            print('  %s: %d edit(s), each applies exactly once' % (target, n))
    return 0


def flatten(text):
    """README as it renders: soft line breaks inside a paragraph read as one space."""
    out, para = [], []
    for line in text.replace('\r\n', '\n').split('\n'):
        if line.strip():
            para.append(line.strip())
        elif para:
            out.append(' '.join(para))
            para = []
    if para:
        out.append(' '.join(para))
    return out


def cmd_same_render(a, b):
    fa, fb = flatten(read(a)[0]), flatten(read(b)[0])
    if fa == fb:
        print('same render: %d paragraphs' % len(fa))
        return 0
    for k, (x, y) in enumerate(zip(fa, fb)):
        if x != y:
            print('paragraph %d differs:\n- %s\n+ %s' % (k + 1, x[:200], y[:200]))
            break
    else:
        print('paragraph counts differ: %d vs %d' % (len(fa), len(fb)))
    return 1


# --- self-test (acceptance criteria of tasks 0443 and 0465) ---------------------------------------------------------

INTRO = '# Changelog\n\nOne entry per round, newest first.\n\n'
OLD = '## 2026-11-01 (round 400): old\n\n**Asked:** old.\n'
README = 'Intro.\n\nFeatures:\na (round 1);\nb (round 2).\nand again (round 3).\n\nEach build: (100 unit checks in 10 tests with libs)\n'


def entry(rnd, title='t'):
    return '## 2026-11-09 (round %d): %s\n\n**Asked:** %s.\n\n**Checked:** CHECKS_LIBS checks.\n' % (rnd, title, title)


USABILITY = '# Usability\n\n| # | Problem | Status |\n|---|---|---|\n| 1 | Menus are long | open |\n| 2 | No search | fixed (round 2) |\n\nNotes.\n'
TOOLS = '# Tools\n\n| Tool | What it does |\n|---|---|\n| `a.py` | does a |\n| `b.py` | does b |\n'
ARCH = '# Architecture\n\n```\nsrc/   the code\ntools/ scripts: a.py, b.py\n```\n\n| Function | Where |\n|---|---|\n| `f()` | `src/f.cpp` |\n'
PERF = '# Performance\n\n## Round 1: first numbers\n\n| row | ms |\n|---|---|\n| a | 1.0 |\n'
DOCS = {'docs/USABILITY.md': USABILITY, 'tools/README.md': TOOLS, 'docs/ARCHITECTURE.md': ARCH, 'docs/PERFORMANCE.md': PERF}


def tree(cl=INTRO + OLD, rd=README, nl='\n', frags=None, docs=None):
    d = tempfile.mkdtemp(prefix='bl0443-')
    os.makedirs(os.path.join(d, 'changelog.d'))
    write(os.path.join(d, 'CHANGELOG.md'), cl, nl)
    write(os.path.join(d, 'README.md'), rd, nl)
    for rel, body in (docs or {}).items():
        os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
        write(os.path.join(d, rel), body, nl)
    write(os.path.join(d, 'changelog.d', 'README.md'), 'the format\n', '\n')
    for name, body in (frags or {}).items():
        write(os.path.join(d, 'changelog.d', name), body, '\n')
    return d


def snapshot(d):
    out = {}
    for base, _, files in os.walk(d):
        for n in files:
            if '.git' in base.split(os.sep):
                continue
            with open(os.path.join(base, n), 'rb') as f:
                out[os.path.relpath(os.path.join(base, n), d)] = f.read()
    return out


def git(d, *a):
    return subprocess.run(['git', '-C', d, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'core.autocrlf=false'] + list(a),
                          capture_output=True, text=True)


def self_test():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print('[%s] %s%s' % ('ok' if ok else 'FAIL', name, (' - ' + detail) if detail and not ok else ''))

    def fails(d, *a):
        try:
            p = plan(d, *a)
            return None, p
        except FoldError as e:
            return str(e), None

    temps = []
    try:
        # 1. Two branches each adding a fragment merge in either order without conflict.
        d = tree()
        temps.append(d)
        git(d, 'init', '-q', '-b', 'main')
        git(d, 'add', '-A')
        git(d, 'commit', '-qm', 'base')
        for b, rnd in (('a', 465), ('b', 463)):
            git(d, 'checkout', '-qb', b, 'main')
            write(os.path.join(d, 'changelog.d', '%d-x%s.md' % (rnd, b)), entry(rnd), '\n')
            git(d, 'add', '-A')
            git(d, 'commit', '-qm', b)
        both = [git(d, 'merge-tree', '--write-tree', x, y).returncode for x, y in (('a', 'b'), ('b', 'a'))]
        check('two passes\' fragments merge in either order without a conflict', both == [0, 0], str(both))

        # 2. Fold order, placement, bodies, newline style kept, fragments deleted, changelog.d/README.md kept.
        frags = {'465-b.md': entry(465, 'B'), '463-a.md': entry(463, 'A'), '466-c.md': entry(466, 'C') + '\n\n'}
        d = tree(cl=INTRO + OLD, nl='\r\n', frags=frags)
        temps.append(d)
        cmd_fold(d, None, False)
        with open(os.path.join(d, 'CHANGELOG.md'), 'rb') as f:
            raw = f.read().decode('utf-8')
        want = INTRO + entry(466, 'C') + '\n' + entry(465, 'B') + '\n' + entry(463, 'A') + '\n' + OLD
        check('fold puts rounds 466, 465, 463 under the intro, above the newest entry, byte for byte', raw == want.replace('\n', '\r\n'))
        check('fold keeps CHANGELOG.md\'s CRLF line endings', '\r\n' in raw and '\n' not in raw.replace('\r\n', ''))
        left = sorted(os.listdir(os.path.join(d, 'changelog.d')))
        check('fold deletes the folded fragments and keeps changelog.d/README.md', left == ['README.md'], str(left))

        # 3. README operations, and all-or-nothing on a missing or ambiguous anchor.
        f1 = entry(470) + README_MARK + '\nafter: a (round 1)\nlight tree (round 470);\n\nreplace: b (round 2).\nwith: b (round 2); denoiser (round 470).\n'
        d = tree(frags={'470-ops.md': f1})
        temps.append(d)
        cmd_fold(d, None, False)
        rd = read(os.path.join(d, 'README.md'))[0]
        check('"after:" inserts below the line it names; "replace:" swaps exact text',
              'a (round 1);\nlight tree (round 470);\nb (round 2); denoiser (round 470).\n' in rd, rd)
        for label, op in (('missing', 'after: no such line\nx\n'), ('ambiguous', 'after: a\nx\n'),
                          ('replace missing', 'replace: zzz\nwith: y\n'), ('replace twice', 'replace: round\nwith: y\n')):
            d = tree(frags={'471-ok.md': entry(471), '472-bad.md': entry(472) + README_MARK + '\n' + op})
            temps.append(d)
            before = snapshot(d)
            err, _ = fails(d)
            code = 0
            try:
                code = cmd_fold(d, None, False)
            except FoldError:
                code = 1
            check('a %s README anchor stops the fold, names the fragment, changes no file' % label,
                  err is not None and '472-bad.md' in err and code == 1 and snapshot(d) == before, str(err))

        # 4. --checks refreshes the count; without it the count stays.
        d = tree(frags={'473-x.md': entry(473)})
        temps.append(d)
        cmd_fold(d, ('20,450', '1760'), False)
        check('fold --checks 20,450 1760 writes (20450 unit checks in 1760 tests',
              '(20450 unit checks in 1760 tests' in read(os.path.join(d, 'README.md'))[0])
        d = tree(frags={'474-x.md': entry(474)})
        temps.append(d)
        cmd_fold(d, None, False)
        check('fold without --checks leaves the count alone', '(100 unit checks in 10 tests' in read(os.path.join(d, 'README.md'))[0])

        # 5. Nothing to fold; --dry-run writes nothing.
        d = tree()
        temps.append(d)
        before = snapshot(d)
        check('fold with no fragments changes nothing', cmd_fold(d, None, False) == 0 and snapshot(d) == before)
        d = tree(frags={'475-x.md': entry(475)})
        temps.append(d)
        before = snapshot(d)
        cmd_fold(d, ('1', '1'), True)
        check('fold --dry-run writes nothing', snapshot(d) == before)

        # 6. check rejects malformed fragments.
        bad = {'no round': ('476-x.md', '## 2026-11-09: no round\n'),
               'name differs from heading': ('477-x.md', entry(478)),
               'unknown README operation': ('479-x.md', entry(479) + README_MARK + '\ninsert: a\nb\n'),
               '"after:" with no lines': ('480-x.md', entry(480) + README_MARK + '\nafter: a (round 1)\n\n'),
               '"replace:" without "with:"': ('481-x.md', entry(481) + README_MARK + '\nreplace: a\n')}
        for label, (name, body) in bad.items():
            d = tree(frags={name: body})
            temps.append(d)
            err, _ = fails(d)
            check('check rejects a fragment with %s' % label, err is not None and name in err, str(err))
        d = tree(frags={'482-x.md': README_MARK + '\nafter: a (round 1)\nx\n'})
        temps.append(d)
        check('check rejects a README section with no entry', fails(d)[0] is not None)

        # 9. Edge cases the first pass missed (test-engineer, task 0443).
        def raw_tree(frag_bytes, **kw):
            d = tree(**kw)
            temps.append(d)
            for n, b in frag_bytes.items():
                with open(os.path.join(d, 'changelog.d', n), 'wb') as f:
                    f.write(b)
            return d

        # CRLF fragment: entry and README section both written with Windows line endings.
        crlf = (entry(490, 'Crlf') + README_MARK + '\nafter: a (round 1)\nnew line (round 490);\n').replace('\n', '\r\n')
        d = raw_tree({'490-crlf.md': crlf.encode('utf-8')})
        cmd_fold(d, None, False)
        with open(os.path.join(d, 'CHANGELOG.md'), 'rb') as f:
            cl = f.read().decode('utf-8')
        with open(os.path.join(d, 'README.md'), 'rb') as f:
            rd = f.read().decode('utf-8')
        check('a CRLF fragment folds into an LF CHANGELOG.md with no stray carriage returns', '\r' not in cl and entry(490, 'Crlf') in cl, repr(cl[:200]))
        check('a CRLF fragment\'s README section applies cleanly (no carriage return in the inserted line)',
              '\r' not in rd and 'a (round 1);\nnew line (round 490);\nb (round 2)' in rd, repr(rd))
        d = tree(nl='\r\n', frags={'491-lf.md': entry(491) + README_MARK + '\nafter: a (round 1)\nlf line (round 491);\n'})
        temps.append(d)
        cmd_fold(d, None, False)
        with open(os.path.join(d, 'README.md'), 'rb') as f:
            rd = f.read().decode('utf-8')
        check('an LF fragment\'s README line lands in a CRLF README.md with CRLF endings',
              'a (round 1);\r\nlf line (round 491);\r\nb (round 2)' in rd and '\n' not in rd.replace('\r\n', ''), repr(rd))

        # Blank lines between README operations and after the last one.
        spaced = entry(492) + README_MARK + '\n\n\nafter: a (round 1)\nX (round 492);\n\n\nreplace: b (round 2).\nwith: B (round 492).\n\n\n\n'
        d = tree(frags={'492-sp.md': spaced})
        temps.append(d)
        cmd_fold(d, None, False)
        rd = read(os.path.join(d, 'README.md'))[0]
        check('blank lines between README operations and after the last one are ignored',
              'a (round 1);\nX (round 492);\nB (round 492).\n' in rd, repr(rd))

        # An "after:" whose insert body has several lines.
        multi = entry(493) + README_MARK + '\nafter: a (round 1)\nfirst (round 493);\nsecond (round 493);\nthird (round 493);\n'
        d = tree(frags={'493-m.md': multi})
        temps.append(d)
        cmd_fold(d, None, False)
        rd = read(os.path.join(d, 'README.md'))[0]
        check('an "after:" with three lines below it inserts all three, in order, right after the anchor',
              'a (round 1);\nfirst (round 493);\nsecond (round 493);\nthird (round 493);\nb (round 2)' in rd, repr(rd))

        # Two fragments: the later round anchors on a line the earlier one inserted.
        d = tree(frags={'495-late.md': entry(495) + README_MARK + '\nafter: early (round 494)\nlate (round 495);\n',
                        '494-early.md': entry(494) + README_MARK + '\nafter: a (round 1)\nearly (round 494);\n'})
        temps.append(d)
        err, _ = fails(d)
        cmd_fold(d, None, False)
        rd = read(os.path.join(d, 'README.md'))[0]
        check('README edits apply in ascending round order, so a later round can anchor on an earlier round\'s line',
              err is None and 'a (round 1);\nearly (round 494);\nlate (round 495);\nb (round 2)' in rd, str(err) + repr(rd))

        # A CHANGELOG.md with no entry yet.
        d = tree(cl='# Changelog\n\nOne entry per round.\n', frags={'496-a.md': entry(496, 'A'), '497-b.md': entry(497, 'B')})
        temps.append(d)
        cmd_fold(d, None, False)
        cl = read(os.path.join(d, 'CHANGELOG.md'))[0]
        check('a CHANGELOG.md with no entry yet gets the entries (newest first) one blank line under the intro',
              cl == '# Changelog\n\nOne entry per round.\n\n' + entry(497, 'B') + '\n' + entry(496, 'A'), repr(cl))

        # Files that are not <digits>-<slug>.md are not fragments.
        stray = {'notes.md': b'scratch, no heading at all\n', 'x-498.md': b'## nope\n', '499.md': b'## nope\n', '500-x.txt': b'## nope\n'}
        d = raw_tree({'498-ok.md': entry(498).encode('utf-8'), **stray})
        err, p = fails(d)
        check('check ignores files not named <round>-<slug>.md (notes.md, x-498.md, 499.md, 500-x.txt)',
              err is None and [f['name'] for f in p['frags']] == ['498-ok.md'], str(err))
        cmd_fold(d, None, False)
        left = sorted(os.listdir(os.path.join(d, 'changelog.d')))
        check('fold leaves files not named <round>-<slug>.md in changelog.d', left == sorted(['README.md'] + list(stray)), str(left))

        # fold --checks with no fragments: the code refreshes the count (the guard only skips when --checks is absent).
        d = tree()
        temps.append(d)
        before = snapshot(d)
        code = cmd_fold(d, ('20,450', '1760'), False)
        after = snapshot(d)
        changed = sorted(k for k in after if after[k] != before[k])
        check('fold --checks with no fragments still refreshes README\'s count and touches nothing else (documented behaviour: criterion 5 reads "changes nothing")',
              code == 0 and changed == ['README.md'] and b'(20450 unit checks in 1760 tests' in after['README.md'], str(changed))
        d = tree()
        temps.append(d)
        before = snapshot(d)
        cmd_fold(d, ('20,450', '1760'), True)
        check('fold --checks --dry-run with no fragments writes nothing', snapshot(d) == before)

        # fill_checks.py (criterion 9): fills the fragment, never README.md.
        d = tree(rd=README + 'CHECKS_W stays\n', frags={'501-f.md': entry(501), '502-nofill.md': '## 2026-11-09 (round 502): t\n\nno placeholders\n'})
        temps.append(d)
        with open(os.path.join(d, 'notes.md'), 'w', encoding='utf-8', newline='') as f:
            f.write('CHECKS_LIBS in a non-fragment\n')
        before = snapshot(d)
        r = subprocess.run([sys.executable, os.path.join(HERE, 'fill_checks.py'), '16,542', '15,767', '15,773', '1638'],
                           capture_output=True, text=True, env=dict(os.environ, BL_W=d))
        after = snapshot(d)
        changed = sorted(k for k in after if after[k] != before[k])
        check('fill_checks.py fills CHECKS_* in the fragment that holds them and leaves README.md, other fragments and non-fragments alone',
              r.returncode == 0 and changed == [os.path.join('changelog.d', '501-f.md')] and b'16,542 checks' in after[changed[0]]
              and b'CHECKS_' not in after[changed[0]], r.stdout + r.stderr + str(changed))
        check('fill_checks.py does not change README\'s unit-check count',
              b'(100 unit checks in 10 tests' in after['README.md'], r.stdout + r.stderr)

        # Review findings (round 467): an anchor inside an indented list item; --checks without its numbers.
        listed = 'Intro.\n\n- **Lighting**: probes;\n  Remove Ringing (Hanning or Lanczos);\n  more.\n\nEach build: (100 unit checks in 10 tests\n'
        d = tree(rd=listed, frags={'491-x.md': entry(491) + README_MARK + '\nafter: Remove Ringing (Hanning\na light tree (round 491);\n'})
        temps.append(d)
        cmd_fold(d, None, False)
        rd = read(os.path.join(d, 'README.md'))[0]
        check('an "after:" anchor matches a list item\'s indented line, and the new line takes its indent',
              '  Remove Ringing (Hanning or Lanczos);\n  a light tree (round 491);\n  more.' in rd, rd)
        d = tree(frags={'492-x.md': entry(492)})
        temps.append(d)
        before = snapshot(d)
        check('fold --checks without its two numbers prints the usage and writes nothing',
              main(['--root', d, 'fold', '--checks', '20,450']) == 2 and snapshot(d) == before)

        d = tree()
        temps.append(d)
        cl = os.path.join(d, 'CHANGELOG.md')
        blocked = os.path.join(d, 'blocked')
        os.makedirs(blocked)  # a directory: the swap onto it fails, as a file an editor holds open does on Windows
        before = snapshot(d)
        try:
            swap([(cl, INTRO + OLD, 'folded\n', '\n'), (blocked, '', 'x\n', '\n')])
            raised = False
        except FoldError:
            raised = True
        check('a fold whose second file cannot be swapped in puts the first back and leaves no temporary',
              raised and snapshot(d) == before and not os.path.exists(blocked + '.fold-tmp'))

        # 10. The shared docs (task 0465): sections for docs/USABILITY.md, tools/README.md, docs/ARCHITECTURE.md and
        # docs/PERFORMANCE.md beside README's, with "after:", "replace:" / "with:" and "append:".
        def doc(d, rel):
            with open(os.path.join(d, rel), 'rb') as f:
                return f.read().decode('utf-8')

        def fold_fails(d):
            before = snapshot(d)
            err, _ = fails(d)
            try:
                code = cmd_fold(d, None, False)
            except FoldError:
                code = 1
            return err, code == 1 and snapshot(d) == before

        block = '## Round 510: the fold, measured\n\n| row | ms |\n|---|---|\n| fold | 0.4 |\n'
        full = (entry(510, 'All') + '\n' + README_MARK + '\nafter: a (round 1)\nshared docs fold (round 510);\n\n'
                '<!-- docs/USABILITY.md -->\nafter: | 2 | No search\n| 3 | Docs conflict | fixed (round 510) |\n\n'
                'replace: | 1 | Menus are long | open |\nwith: | 1 | Menus are long | fixed (round 510) |\n\n'
                '<!-- tools/README.md -->\nafter: | `a.py`\n| `aa.py` | does aa (round 510) |\n\n'
                '<!-- docs/ARCHITECTURE.md -->\nreplace: scripts: a.py, b.py\nwith: scripts: a.py, aa.py, b.py\n\n'
                'after: | `f()`\n| `g()` | `src/g.cpp` |\n\n'
                '<!-- docs/PERFORMANCE.md -->\nappend:\n\n' + block)
        d = tree(frags={'510-all.md': full}, docs=DOCS)
        temps.append(d)
        err, _ = fails(d)
        cmd_fold(d, None, False)
        check('a fragment with README and all four shared-docs sections is well formed', err is None, str(err))
        check('docs/USABILITY.md: "after:" adds the row under the row it names, "replace:" changes a row\'s status',
              doc(d, 'docs/USABILITY.md') == USABILITY.replace('| 1 | Menus are long | open |', '| 1 | Menus are long | fixed (round 510) |')
              .replace('| 2 | No search | fixed (round 2) |\n', '| 2 | No search | fixed (round 2) |\n| 3 | Docs conflict | fixed (round 510) |\n'),
              doc(d, 'docs/USABILITY.md'))
        check('tools/README.md: the new tool\'s row lands under the row it names',
              doc(d, 'tools/README.md') == TOOLS.replace('| `a.py` | does a |\n', '| `a.py` | does a |\n| `aa.py` | does aa (round 510) |\n'),
              doc(d, 'tools/README.md'))
        check('docs/ARCHITECTURE.md: a "replace:" and an "after:" in one section both apply',
              doc(d, 'docs/ARCHITECTURE.md') == ARCH.replace('scripts: a.py, b.py', 'scripts: a.py, aa.py, b.py')
              .replace('| `f()` | `src/f.cpp` |\n', '| `f()` | `src/f.cpp` |\n| `g()` | `src/g.cpp` |\n'), doc(d, 'docs/ARCHITECTURE.md'))
        check('docs/PERFORMANCE.md: "append:" puts the block at the file\'s end, one blank line above it, one newline after it',
              doc(d, 'docs/PERFORMANCE.md') == PERF + '\n' + block, repr(doc(d, 'docs/PERFORMANCE.md')))
        check('the same fold still applies the README section and the CHANGELOG entry',
              'a (round 1);\nshared docs fold (round 510);\nb (round 2)' in doc(d, 'README.md') and entry(510, 'All') in doc(d, 'CHANGELOG.md'))
        check('fold deletes the fragment once all five files are written', sorted(os.listdir(os.path.join(d, 'changelog.d'))) == ['README.md'])

        d = tree(nl='\r\n', frags={'511-crlf.md': full.replace('510', '511')}, docs=DOCS)
        temps.append(d)
        cmd_fold(d, None, False)
        raws = [doc(d, rel) for rel in DOCS]
        check('the shared docs keep their CRLF line endings, appended and inserted lines included',
              all('\n' not in r.replace('\r\n', '') and '\r\n' in r for r in raws)
              and doc(d, 'docs/PERFORMANCE.md') == (PERF + '\n' + block.replace('510', '511')).replace('\n', '\r\n'))

        # Parallel passes: two fragments each add a row and a section; ascending round order, whatever the file names.
        d = tree(docs=DOCS, frags={
            '521-b.md': entry(521) + '<!-- docs/USABILITY.md -->\nafter: | 3 | From round 520\n| 4 | From round 521 | fixed |\n'
                        '<!-- docs/PERFORMANCE.md -->\nappend:\n## Round 521\nb\n',
            '520-a.md': entry(520) + '<!-- docs/USABILITY.md -->\nafter: | 2 | No search\n| 3 | From round 520 | fixed |\n'
                        '<!-- docs/PERFORMANCE.md -->\nappend:\n## Round 520\n\na\n'})
        temps.append(d)
        cmd_fold(d, None, False)
        check('two passes\' rows fold in ascending round order, so a later round can anchor on an earlier round\'s row',
              '| 2 | No search | fixed (round 2) |\n| 3 | From round 520 | fixed |\n| 4 | From round 521 | fixed |\n' in doc(d, 'docs/USABILITY.md'),
              doc(d, 'docs/USABILITY.md'))
        check('two "append:" blocks land at the end in round order, a blank line between them',
              doc(d, 'docs/PERFORMANCE.md') == PERF + '\n## Round 520\n\na\n\n## Round 521\nb\n', repr(doc(d, 'docs/PERFORMANCE.md')))

        # Exactly once, all or nothing: a bad operation in any section stops the fold before any file is written.
        stops = {
            'a missing docs/USABILITY.md anchor': '<!-- docs/USABILITY.md -->\nafter: | 9 | no such row\n| x |\n',
            'an ambiguous tools/README.md anchor': '<!-- tools/README.md -->\nafter: |\n| x |\n',
            'a docs/ARCHITECTURE.md "replace:" text found twice': '<!-- docs/ARCHITECTURE.md -->\nreplace: .py\nwith: .txt\n',
            'an "append:" whose block is already in docs/PERFORMANCE.md': '<!-- docs/PERFORMANCE.md -->\nappend:\n## Round 1: first numbers\nagain\n',
        }
        for label, section in stops.items():
            good = entry(530) + README_MARK + '\nafter: a (round 1)\nfine (round 530);\n<!-- docs/USABILITY.md -->\nafter: | 2 | No search\n| 3 | fine |\n'
            d = tree(docs=DOCS, frags={'530-ok.md': good, '531-bad.md': entry(531) + section})
            temps.append(d)
            err, unchanged = fold_fails(d)
            check('%s stops the fold, names the fragment and the file, changes no file' % label,
                  err is not None and '531-bad.md' in err and section.split(' ')[1] in err and unchanged, str(err))
        twice = {'540-a.md': entry(540) + '<!-- docs/PERFORMANCE.md -->\nappend:\n## Pass 9\none\n',
                 '541-b.md': entry(541) + '<!-- docs/PERFORMANCE.md -->\nappend:\n## Pass 9\ntwo\n'}
        d = tree(docs=DOCS, frags=twice)
        temps.append(d)
        err, unchanged = fold_fails(d)
        check('two fragments appending a block with the same first line: the second is refused, nothing is written',
              err is not None and '541-b.md' in err and unchanged, str(err))
        d = tree(frags={'542-x.md': entry(542) + '<!-- docs/USABILITY.md -->\nafter: | 2 |\n| x |\n'})
        temps.append(d)
        err, unchanged = fold_fails(d)
        check('a section for a file the tree does not have is refused, with the fragment\'s and the file\'s name',
              err is not None and '542-x.md' in err and 'docs/USABILITY.md' in err and unchanged, str(err))

        malformed = {'an unknown file ("<!-- docs/requirements.md -->")': '<!-- docs/requirements.md -->\nafter: a\nb\n',
                     'the same section twice': '<!-- tools/README.md -->\nafter: | `a.py`\n| x |\n<!-- tools/README.md -->\nafter: | `b.py`\n| y |\n',
                     '"append:" with no lines below it': '<!-- docs/PERFORMANCE.md -->\nappend:\n\n',
                     '"append:" with text on its own line': '<!-- docs/PERFORMANCE.md -->\nappend: ## Round 550\nrow\n',
                     'a "with:" that follows no "replace:" in a shared-docs section': '<!-- docs/USABILITY.md -->\nwith: x\n'}
        for label, section in malformed.items():
            d = tree(docs=DOCS, frags={'550-x.md': entry(550) + section})
            temps.append(d)
            err, _ = fails(d)
            check('check rejects a fragment with %s' % label, err is not None and '550-x.md' in err, str(err))

        note = entry(551).replace('**Asked:**', '<!-- a note for the reviewer -->\n**Asked:**')
        d = tree(docs=DOCS, frags={'551-x.md': note + '<!-- docs/USABILITY.md -->\nafter: | 2 |\n| 3 | x |\n'})
        temps.append(d)
        err, p = fails(d)
        check('an HTML comment in the entry that names no file stays in the entry',
              err is None and '<!-- a note for the reviewer -->' in p['frags'][0]['entry'], str(err))

        d = tree(docs=DOCS, frags={'552-x.md': full.replace('510', '552')})
        temps.append(d)
        before = snapshot(d)
        code = main(['--root', d, 'fold', '--dry-run'])
        check('fold --dry-run with shared-docs sections writes nothing', code == 0 and snapshot(d) == before)
        check('check passes the same tree', main(['--root', d, 'check']) == 0 and snapshot(d) == before)

        with open(os.path.join(HERE, 'docs_check.sh'), encoding='utf-8') as f:
            listed = re.search(r'^SHARED_DOCS="([^"]*)"', f.read(), re.M)
        check('docs_check.sh and changelog.py name the same shared docs', listed is not None and tuple(listed.group(1).split()) == SHARED_DOCS)

        # 8. The render comparison.
        a = tree()
        temps.append(a)
        pa = os.path.join(a, 'README.md')
        pb = os.path.join(a, 'split.md')
        write(pb, README.replace('a (round 1);\nb', 'a (round 1); b'), '\r\n')
        check('same-render: a paragraph split over lines reads the same', cmd_same_render(pa, pb) == 0)
        write(pb, README.replace('b (round 2)', 'b (round 3)'), '\n')
        check('same-render: a changed word is reported', cmd_same_render(pa, pb) == 1)

        # 7. docs_check.sh on a throwaway repository (needs bash).
        bash = shutil.which('bash')
        if not bash:
            print('[skip] docs_check.sh cases: no bash on PATH')
        else:
            d = tree()
            temps.append(d)
            os.makedirs(os.path.join(d, 'tools', 'dev'))
            for n in ('docs_check.sh', 'changelog.py', 'fill_checks.py'):
                shutil.copy(os.path.join(HERE, n), os.path.join(d, 'tools', 'dev', n))
            git(d, 'init', '-q', '-b', 'main')
            git(d, 'add', '-A')
            git(d, 'commit', '-qm', 'base')

            def run_check(branch, edit, msg):
                git(d, 'checkout', '-qb', branch, 'main')
                edit()
                git(d, 'add', '-A')
                git(d, 'commit', '-qm', msg)
                r = subprocess.run([bash, 'tools/dev/docs_check.sh', 'main', 'HEAD'], cwd=d, capture_output=True, text=True,
                                   env=dict(os.environ, NO_FRAGMENTS='1'))
                return r.returncode

            def touch_cl():
                with open(os.path.join(d, 'CHANGELOG.md'), 'a', encoding='utf-8') as f:
                    f.write('x\n')

            def add_frag():
                write(os.path.join(d, 'changelog.d', '490-y.md'), entry(490), '\n')

            check('docs_check fails a commit editing CHANGELOG.md without the trailer', run_check('c1', touch_cl, 'round') == 1)
            check('docs_check passes it with "%s"' % TRAILER, run_check('c2', touch_cl, 'write-up\n\n' + TRAILER) == 0)
            check('docs_check passes a branch that only adds a fragment', run_check('c3', add_frag, 'round') == 0)
            git(d, 'checkout', '-q', 'c3')
            git(d, 'merge', '-q', '--no-ff', '-m', 'merge', 'c1')
            r = subprocess.run([bash, 'tools/dev/docs_check.sh', 'c1', 'HEAD'], cwd=d, capture_output=True, text=True,
                               env=dict(os.environ, NO_FRAGMENTS='1'))
            check('docs_check ignores merge commits and commits already on the base', r.returncode == 0, r.stdout + r.stderr)

            def touch_readme():
                with open(os.path.join(d, 'README.md'), 'a', encoding='utf-8') as f:
                    f.write('x\n')

            check('docs_check fails a commit editing README.md only, without the trailer', run_check('r1', touch_readme, 'round') == 1)
            check('docs_check passes a README.md edit with the trailer', run_check('r2', touch_readme, 'fold\n\n' + TRAILER) == 0)
            check('docs_check accepts the trailer in lower case ("docs-fold: yes")', run_check('r3', touch_cl, 'fold\n\ndocs-fold: yes') == 0)
            check('docs_check accepts the trailer when the message has CRLF line endings',
                  run_check('r4', touch_cl, 'fold\r\n\r\n' + TRAILER + '\r\n') == 0)
            check('docs_check rejects "Docs-fold: no" and a trailer buried in a sentence',
                  run_check('r5', touch_cl, 'fold\n\nDocs-fold: no') == 1 and run_check('r6', touch_cl, 'say Docs-fold: yes here') == 1)

            def docs_check(*refs, fragments=False):
                env = dict(os.environ)
                if not fragments:
                    env['NO_FRAGMENTS'] = '1'
                return subprocess.run([bash, 'tools/dev/docs_check.sh'] + list(refs), cwd=d, capture_output=True,
                                      text=True, env=env).returncode

            check('docs_check refuses a base that is not a commit instead of passing', docs_check('no-such-ref', 'HEAD') == 1)
            git(d, 'checkout', '-q', '--orphan', 'nested')
            git(d, 'rm', '-rq', '--cached', '.')
            os.makedirs(os.path.join(d, 'blendity'), exist_ok=True)
            shutil.copy(os.path.join(d, 'CHANGELOG.md'), os.path.join(d, 'blendity', 'CHANGELOG.md'))
            git(d, 'add', 'blendity')
            git(d, 'commit', '-qm', 'nested')
            check('docs_check refuses a tree with the files under blendity/ (the libraries clone) instead of passing',
                  docs_check('main', 'nested') == 1)
            git(d, 'checkout', '-qf', 'c3')
            check('docs_check runs changelog.py check with a Python that works', docs_check('c1', 'HEAD', fragments=True) == 0)

            # The shared docs (task 0465): the same rule for docs/USABILITY.md, tools/README.md, docs/ARCHITECTURE.md
            # and docs/PERFORMANCE.md. This repository's base commit holds the script with the list, so every commit
            # of a branch descends from the rule and is checked.
            def touch(rel):
                def edit():
                    os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
                    with open(os.path.join(d, rel), 'a', encoding='utf-8') as f:
                        f.write('x\n')
                return edit

            for k, rel in enumerate(SHARED_DOCS):
                check('docs_check fails a commit editing %s without the trailer' % rel, run_check('s%da' % k, touch(rel), 'round') == 1)
                check('docs_check passes a %s edit with the trailer (a fold)' % rel, run_check('s%db' % k, touch(rel), 'fold\n\n' + TRAILER) == 0)
            check('docs_check leaves other docs alone (docs/tasks, tools/dev/README.md)',
                  run_check('s9', lambda: (touch('docs/tasks/0001-x.md')(), touch('tools/dev/README.md')()), 'round') == 0)

            # A commit written before the shared docs joined the rule is exempt for them; one made after its branch
            # merged the rule is not. A second repository, where main gains the script after a first commit.
            d3 = tree(docs=DOCS)
            temps.append(d3)
            git(d3, 'init', '-q', '-b', 'main')
            git(d3, 'add', '-A')
            git(d3, 'commit', '-qm', 'before the rule')
            git(d3, 'checkout', '-qb', 'old', 'main')
            with open(os.path.join(d3, 'docs', 'USABILITY.md'), 'a', encoding='utf-8') as f:
                f.write('| 3 | a row the old way | fixed |\n')
            git(d3, 'commit', '-qam', 'a round the old way')
            git(d3, 'checkout', '-q', 'main')
            os.makedirs(os.path.join(d3, 'tools', 'dev'))
            for n in ('docs_check.sh', 'changelog.py', 'fill_checks.py'):
                shutil.copy(os.path.join(HERE, n), os.path.join(d3, 'tools', 'dev', n))
            git(d3, 'add', '-A')
            git(d3, 'commit', '-qm', 'the shared docs join the rule')
            git(d3, 'checkout', '-q', 'old')

            def shared_check():
                return subprocess.run([bash, 'tools/dev/docs_check.sh', 'main', 'HEAD'], cwd=d3, capture_output=True,
                                      text=True, env=dict(os.environ, NO_FRAGMENTS='1'))

            git(d3, 'merge', '-q', '--no-ff', '-m', 'merge main', 'main')
            r = shared_check()
            check('docs_check exempts a shared-docs edit written before the shared docs joined the rule, and says so',
                  r.returncode == 0 and 'exempt:' in r.stdout and 'docs/USABILITY.md' in r.stdout, r.stdout + r.stderr)
            with open(os.path.join(d3, 'tools', 'README.md'), 'a', encoding='utf-8') as f:
                f.write('| `c.py` | a row after merging main |\n')
            git(d3, 'commit', '-qam', 'a round after merging main')
            r = shared_check()
            check('docs_check fails a shared-docs edit made after its branch merged the rule, naming the file',
                  r.returncode == 1 and 'tools/README.md' in r.stdout, r.stdout + r.stderr)
    finally:
        for t in temps:
            shutil.rmtree(t, ignore_errors=True)
    failed = results.count(False)
    print('self-test: %d passed, %d failed' % (len(results) - failed, failed))
    return 1 if failed else 0


def main(argv):
    root = os.path.dirname(os.path.dirname(HERE))
    if '--root' in argv:
        k = argv.index('--root')
        root = argv[k + 1]
        del argv[k:k + 2]
    if argv[:1] == ['--self-test']:
        return self_test()
    try:
        if argv[:1] == ['check']:
            return cmd_check(root)
        if argv[:1] == ['fold']:
            checks = None
            if '--checks' in argv:
                k = argv.index('--checks')
                checks = tuple(argv[k + 1:k + 3])
                if len(checks) != 2 or not all(re.match(r'^[\d,]+$', c) for c in checks):
                    print('fold --checks needs two numbers: LIBS TESTS (e.g. --checks 20,450 1760)\n')
                    print(__doc__)
                    return 2
            return cmd_fold(root, checks, '--dry-run' in argv)
        if argv[:1] == ['same-render'] and len(argv) == 3:
            return cmd_same_render(argv[1], argv[2])
    except FoldError as e:
        print('changelog.py: %s\nNothing was written.' % e)
        return 1
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
