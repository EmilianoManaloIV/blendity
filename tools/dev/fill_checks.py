"""Fills a round's CHECKS_* placeholders in CHANGELOG.md (and extra files) and the README's check count from a chain's
numbers (tools/dev/README.md).
Usage: py tools/dev/fill_checks.py LIBS W LINUX TESTS [file ...]   e.g. 16,542 15,767 15,773 1638 pr.md commit.txt"""
import io, os, re, sys

if len(sys.argv) < 5:
    sys.exit(__doc__)
libs, w, linux, tests = sys.argv[1:5]
root = os.environ.get('BL_W', 'W:/blendity')
rep = {'CHECKS_LIBS': libs, 'CHECKS_W': w, 'CHECKS_L': linux}


def fill(path, fn):
    s = io.open(path, encoding='utf-8', newline='').read()
    t = fn(s)
    io.open(path, 'w', encoding='utf-8', newline='').write(t)
    print('filled' if s != t else 'unchanged', path)


def checks(s):
    for k, v in rep.items():
        s = s.replace(k, v)
    return s


fill(os.path.join(root, 'CHANGELOG.md'), checks)
fill(os.path.join(root, 'README.md'), lambda s: re.sub(r'\(\d+ unit checks in \d+ tests', '(%s unit checks in %s tests' % (libs.replace(',', ''), tests), s, count=1))
for extra in sys.argv[5:]:
    fill(extra, checks)
