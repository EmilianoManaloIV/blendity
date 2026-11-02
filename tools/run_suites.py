#!/usr/bin/env python3
"""Run blendity_tests' groups as parallel processes (task 0371): every group of every build given its own process, its
own test folder (BLENDITY_TEST_DIR: a shared one made one group's files another's), its share of the cores
(BLENDITY_THREADS) and its build's own cache folder (BLENDITY_CACHE_DIR), in its build's own tree (the tests read
tests/fixtures and docs/ from the working folder), longest first by the last full run's times. The same group of two
builds never runs at once (Live Link's tests take fixed ports). Prints each build's checks, failures and wall time and
the [FAIL] lines; exits 1 on any failure, crash, timeout, missing summary, a build with no groups or no checks.

Usage: python tools/run_suites.py <blendity_tests exe> [<another build's exe> ...] [--jobs N] [--out DIR] [--filter TEXT]
                                  [--timeout SECONDS]
  --jobs     processes at once (default: the cores / 4, at least 2)
  --out      where the groups' folders, logs and times go (default: <temp>/blendity_suites); keep it short - the
             Unreal bridge's install test writes paths near Windows' 260-character limit
  --filter   passed on as BLENDITY_TEST_FILTER (the recorded times aren't updated by a filtered run)
  --timeout  a group's limit (default 3600 s)
The bridge tests (BLENDITY_TEST_BRIDGE) open one engine project: set it for one build's run only.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

SUMMARY = re.compile(r"^(\d+) checks, (\d+) failed")
GROUP_NAME = re.compile(r"[a-z][a-z0-9_]*")  # (a group's name: anything else a program prints isn't one - never a path, never `_cache`)


def tree_of(exe):
    """The build's source tree: three folders above dist/<platform>/<exe>; None when it has no tests/fixtures."""
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(exe))))
    return root if os.path.isdir(os.path.join(root, 'tests', 'fixtures')) else None


def groups_of(exe, cwd):
    """The build's groups, or (None, why) when it doesn't run or names none."""
    try:
        p = subprocess.run([exe, '--list-groups'], capture_output=True, text=True, encoding='utf-8', errors='replace', cwd=cwd, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        return None, str(e)
    names = [g.strip() for g in p.stdout.splitlines() if GROUP_NAME.fullmatch(g.strip())]
    if p.returncode != 0 or not names:
        return None, 'exit %d, %d group(s)%s' % (p.returncode, len(names), (': ' + p.stderr.strip()[:200]) if p.stderr.strip() else '')
    return names, ''


def label_of(exe, seen):
    """A short name for a build: the two folders above dist/<platform> ("W-blendity", "blender-main-blendity"), unique."""
    parts = os.path.normpath(os.path.abspath(exe)).split(os.sep)
    base = '-'.join(parts[-5:-3]) if len(parts) >= 5 else os.path.basename(exe)
    base = base.replace(':', '')
    name, k = base, 2
    while name in seen:
        name, k = '%s_%d' % (base, k), k + 1
    seen.add(name)
    return name


def fresh_folder(out, label, group):
    """The group's own folder, emptied - only ever inside `out` (review 394: a name from a program's output is checked)."""
    folder = os.path.abspath(os.path.join(out, label, group))
    base = os.path.abspath(out)
    if folder == base or os.path.commonpath([folder, base]) != base:
        raise ValueError('not inside the output folder: ' + folder)
    shutil.rmtree(folder, ignore_errors=True)
    if os.path.exists(folder):
        print('warning: %s could not be emptied (a process still running there?)' % folder, flush=True)
    os.makedirs(folder, exist_ok=True)
    return folder


def run_group(task, out, threads, filt, timeout):
    exe, label, group, cwd = task
    folder = fresh_folder(out, label, group)
    env = dict(os.environ, BLENDITY_TEST_DIR=folder, BLENDITY_THREADS=str(threads), BLENDITY_CACHE_DIR=os.path.join(os.path.abspath(out), label, '_cache'))
    if filt:
        env['BLENDITY_TEST_FILTER'] = filt
    t0 = time.time()
    try:
        p = subprocess.run([exe, '--group', group], capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, cwd=cwd, timeout=timeout)
        text, code = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        text, code = ((e.stdout or '') if isinstance(e.stdout, str) else '') + '\n(timed out after %d s)' % timeout, -999
    with open(os.path.join(out, label, group + '.log'), 'w', encoding='utf-8') as f:
        f.write(text)
    summary = [m for m in (SUMMARY.match(l) for l in text.splitlines()) if m]
    checks, failed = (int(summary[-1].group(1)), int(summary[-1].group(2))) if summary else (0, -1)
    fails = [l for l in text.splitlines() if l.startswith('[FAIL]') or l.startswith('  FAIL ')]
    return {'label': label, 'group': group, 'exit': code, 'checks': checks, 'failed': failed, 'fails': fails, 'seconds': time.time() - t0}


class Scheduler:
    """Hands out the tasks longest first, never a group already running for another build."""

    def __init__(self, tasks):
        self.tasks, self.running, self.lock = list(tasks), set(), threading.Condition()

    def take(self):
        with self.lock:
            while self.tasks:
                pick = next((t for t in self.tasks if t[2] not in self.running), None)
                if pick:
                    self.tasks.remove(pick)
                    self.running.add(pick[2])
                    return pick
                self.lock.wait()
            return None

    def done(self, task):
        with self.lock:
            self.running.discard(task[2])
            self.lock.notify_all()


def parse(argv):
    ap = argparse.ArgumentParser(description='Run blendity_tests groups as parallel processes.')
    ap.add_argument('exes', nargs='+', help='blendity_tests executables (one per build)')
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--out', default=os.path.join(tempfile.gettempdir(), 'blendity_suites'))
    ap.add_argument('--filter', default='')
    ap.add_argument('--timeout', type=int, default=3600)
    a = ap.parse_args(argv[1:])
    if a.jobs < 0 or a.timeout <= 0:
        ap.error('--jobs must be 0 or more and --timeout positive')
    return a


def main(argv):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')  # (a failing check's text may be Japanese: cp1252 can't print it)
    except AttributeError:
        pass
    a = parse(argv)
    a.exes = [os.path.abspath(e) for e in a.exes]  # (re-check 394: each group runs in its tree - a relative exe would be looked for there)
    cores = os.cpu_count() or 4
    jobs = a.jobs or max(2, cores // 4)
    threads = max(2, cores // jobs)
    out = a.out
    if len(os.path.abspath(out)) > 60:
        print('warning: --out is long (%d characters): the Unreal bridge test may hit the path limit' % len(os.path.abspath(out)))
    os.makedirs(out, exist_ok=True)
    times_path = os.path.join(out, 'times.json')
    try:
        with open(times_path, encoding='utf-8') as f:
            times = json.load(f)
    except (OSError, ValueError):
        times = {}
    seen, tasks, broken_builds = set(), [], []
    for exe in a.exes:
        label = label_of(exe, seen)
        cwd = tree_of(exe)
        groups, why = groups_of(exe, cwd) if cwd else (None, 'no tests/fixtures three folders above it: not a build in its tree')
        if not groups:
            broken_builds.append((label, exe, why))
            continue
        os.makedirs(os.path.join(out, label), exist_ok=True)
        tasks += [(exe, label, g, cwd) for g in groups]
    tasks.sort(key=lambda t: (-times.get(t[2], 1e9), t[1]))  # (the longest first; a group never timed first of all)
    print('%d groups of %d build(s) on %d processes, %d threads each' % (len(tasks), len(a.exes) - len(broken_builds), jobs, threads), flush=True)
    t0 = time.time()
    results, sched, lock = [], Scheduler(tasks), threading.Lock()

    def worker():
        while True:
            t = sched.take()
            if t is None:
                return
            try:
                r = run_group(t, out, threads, a.filter, a.timeout)
            except Exception as e:  # (re-check 394: a locked exe, a folder that won't empty - the group fails, the run goes on)
                r = {'label': t[1], 'group': t[2], 'exit': -998, 'checks': 0, 'failed': -1, 'fails': ['the runner: %s: %s' % (type(e).__name__, e)], 'seconds': 0.0}
            finally:
                sched.done(t)
            with lock:
                results.append(r)
                mark = 'ok' if r['exit'] == 0 and r['failed'] == 0 else 'FAIL'
                print('  [%s] %s %s: %d checks, %d failed, %.0f s' % (mark, r['label'], r['group'], r['checks'], r['failed'], r['seconds']), flush=True)

    pool = [threading.Thread(target=worker) for _ in range(jobs)]
    for th in pool:
        th.start()
    for th in pool:
        th.join()
    if not a.filter:  # (a filtered run times every group at nothing: the order would be lost)
        for r in results:
            if r['exit'] == 0 and r['failed'] >= 0:
                times[r['group']] = max(times.get(r['group'], 0.0) * 0.5, r['seconds'])  # (a group's slowest build, decayed)
        with open(times_path, 'w', encoding='utf-8') as f:
            json.dump(times, f, indent=1, sort_keys=True)
    bad = bool(broken_builds) or len(results) != len(tasks)  # (every group accounted for)
    if len(results) != len(tasks):
        print('%d group(s) gave no result' % (len(tasks) - len(results)))
    for label, exe, why in broken_builds:
        print('%s: did not run (%s): %s' % (label, exe, why))
    for label in sorted({r['label'] for r in results}):
        mine = [r for r in results if r['label'] == label]
        checks, failed = sum(r['checks'] for r in mine), sum(max(0, r['failed']) for r in mine)
        broken = [r for r in mine if r['exit'] != 0 or r['failed'] != 0]
        note = ' - %d group(s) failed or crashed' % len(broken) if broken else ''
        if checks == 0 and not broken:
            note, bad = ' - no checks ran (a filter that matches nothing?)', True
        print('%s: %d checks, %d failed%s' % (label, checks, failed, note))
        for r in broken:
            bad = True
            print('  %s (exit %d%s):' % (r['group'], r['exit'], ', no summary' if r['failed'] < 0 else ''))
            for l in r['fails'][:20]:
                print('    ' + l[:220])
    print('wall time %.0f s' % (time.time() - t0))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
