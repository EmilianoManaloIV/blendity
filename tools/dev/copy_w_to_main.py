"""The dependency-free tree's changes (against HEAD) into the Blender-libraries clone (tools/dev/README.md), retried when
Windows refuses a file for a moment (an indexer or IDE holding it), every copy checked byte for byte; exits 1 if any
file still differs. BL_W / BL_MAIN override the two trees."""
import filecmp, os, shutil, subprocess, sys, time

W = os.environ.get('BL_W', 'W:/blendity')
M = os.environ.get('BL_MAIN', 'C:/Users/trick/Downloads/blender-main/blendity')
SKIP = ('Assets/', 'dist/', 'build/')


def git(*a):
    return subprocess.run(['git', '-C', W] + list(a), capture_output=True, text=True, encoding='utf-8').stdout.splitlines()


def copy(src, dst):
    last = None
    for _ in range(20):
        try:
            shutil.copyfile(src, dst)
            return True
        except OSError as e:
            last = e
            time.sleep(0.5)
    print('FAILED', dst, last)
    return False


def strip(rel):
    return rel[len('blendity/'):] if rel.startswith('blendity/') else rel


def same(src, dst):
    return os.path.exists(dst) and filecmp.cmp(src, dst, shallow=False)


changed = git('diff', '--name-only', 'HEAD', '--', '.')
untracked = git('ls-files', '--others', '--exclude-standard', '--', '.')
n, bad = 0, 0
for rel in map(strip, changed + untracked):
    if rel.startswith(SKIP):
        continue
    src, dst = os.path.join(W, rel).replace('\\', '/'), os.path.join(M, rel).replace('\\', '/')
    if not os.path.exists(src):
        if os.path.exists(dst):
            os.remove(dst)
            print('removed', rel)
        continue
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not same(src, dst):
        if not copy(src, dst):
            bad += 1
            continue
        print('copied', rel)
    if not same(src, dst):
        print('DIFFERS', rel)
        bad += 1
    n += 1

# Files the clone changed that W hasn't (W reverted an experiment): the clone takes HEAD's own copy.
w_rels = set(map(strip, changed + untracked))
main_changed = subprocess.run(['git', '-C', M, 'diff', '--name-only', 'HEAD', '--', '.'], capture_output=True, text=True, encoding='utf-8').stdout.splitlines()
for rel in map(strip, main_changed):
    if rel in w_rels or rel.startswith(SKIP) or not os.path.exists(os.path.join(W, rel)):
        continue
    r = subprocess.run(['git', '-C', M, 'checkout', '--', rel], capture_output=True, text=True)
    if r.returncode == 0:
        print('restored', rel)
    else:
        print('FAILED to restore', rel, r.stderr.strip())
        bad += 1
print(n, 'files', '(%d bad)' % bad if bad else '')
sys.exit(1 if bad else 0)
