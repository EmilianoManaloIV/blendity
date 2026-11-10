"""Fills a round's CHECKS_* placeholders in its changelog fragment (changelog.d/*.md, ADR 0123) and extra files from a
chain's numbers (tools/dev/README.md). README's check count is no longer a round's: `changelog.py fold --checks`
refreshes it when the pass write-up folds the fragments (readme_count below is the one rule both use).
Usage: py tools/dev/fill_checks.py LIBS W LINUX TESTS [file ...]   e.g. 16,542 15,767 15,773 1638 pr.md commit.txt"""
import glob, io, os, re, sys


def readme_count(s, libs, tests):
    """README's `(N unit checks in M tests` with the numbers given (LIBS may carry thousands commas)."""
    return re.sub(r'\(\d+ unit checks in \d+ tests', '(%s unit checks in %s tests' % (libs.replace(',', ''), tests), s, count=1)


def fill(path, fn):
    s = io.open(path, encoding='utf-8', newline='').read()
    t = fn(s)
    io.open(path, 'w', encoding='utf-8', newline='').write(t)
    print('filled' if s != t else 'unchanged', path)


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    libs, w, linux, _tests = argv[:4]
    root = os.environ.get('BL_W', 'W:/blendity')
    rep = {'CHECKS_LIBS': libs, 'CHECKS_W': w, 'CHECKS_L': linux}

    def checks(s):
        for k, v in rep.items():
            s = s.replace(k, v)
        return s

    frags = [p for p in sorted(glob.glob(os.path.join(root, 'changelog.d', '[0-9]*-*.md')))
             if 'CHECKS_' in io.open(p, encoding='utf-8').read()]
    if not frags:
        print('no changelog.d fragment holds CHECKS_ placeholders')
    for path in frags + argv[4:]:
        fill(path, checks)


if __name__ == '__main__':
    main(sys.argv[1:])
