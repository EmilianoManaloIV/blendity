"""Run the dependency-free animation scheduling check with inherited Linux CPU masks."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    allowed = sorted(os.sched_getaffinity(0))
    pair = [allowed[0], allowed[-1]]
    with tempfile.TemporaryDirectory(prefix="blendity-affinity-") as root:
        for mask in dict.fromkeys((tuple(allowed[:1]), tuple(sorted(set(pair))), tuple(allowed))):
            with tempfile.TemporaryDirectory(dir=root) as scratch:
                env = os.environ.copy()
                env.pop("BLENDITY_THREADS", None)
                env["HOME"] = scratch
                for name in ("TEST_DIR", "PROJECT", "PREFS_DIR", "CACHE_DIR", "TRASH"):
                    env["BLENDITY_" + name] = str(Path(scratch) / name)
                def check(group, name):
                    env["BLENDITY_TEST_FILTER"] = name
                    run = subprocess.run(
                        ["taskset", "--cpu-list", ",".join(map(str, mask)), binary, "--group", group],
                        env=env, cwd=scratch, capture_output=True, text=True, timeout=60,
                    )
                    print(f"Mask {mask}: {run.stdout}", flush=True)
                    run.check_returncode()
                    if name not in run.stdout or "0 failed" not in run.stdout:
                        raise AssertionError(f"Test did not run: {name}")
                    return run.stdout

                foreground = check("animation", "min spec: a thread ceiling")
                counts = re.search(r"ceiling 3: (\d+) threads ran the loop \(the machine has (\d+)\), thread_count\(\) says (\d+)", foreground)
                if not counts:
                    raise ValueError("Missing scheduling results")
                ran, detected, capped = map(int, counts.groups())
                if detected != len(mask) or capped != min(3, len(mask)) or not 1 <= ran <= capped:
                    raise AssertionError(f"Unexpected concurrency for mask {mask}: {counts.groups()}")
                check("editor", "the job system's background pool")
                check("animation", "CPU affinity: explicit worker counts")


if __name__ == "__main__":
    main()
