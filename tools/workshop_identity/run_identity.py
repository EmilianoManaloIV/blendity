#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the Workshop identity acceptance group in five isolated editor processes."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


PHASES = (
    ("seed", "v1", "Installed-A", "WorkshopIdentity.vpk"),
    ("offline-v1", None, None, None),
    ("reimport-v2", "v2", "Installed-B", "WorkshopIdentity.vpk"),
    ("repeat-v2", "v2", "Installed-C", "WorkshopIdentity.vpk"),
    ("offline-v2", None, None, None),
)
FIXTURE_SHA256 = {
    "v1": "cfa8619cea2e6fc63c1f643c84bc9d719014fc7fbe691f5227600a697e5dbac6",
    "v2": "938ee7bf83da8665d6c0f3eddc88d518c60dc910580364c2eb47c1a44c417803",
}


def run_phase(exe, repo, out, sources, sequence, phase, texture_policy):
    name, version, install_dir, item_name = phase
    source = None
    if version:
        source = out / install_dir / item_name
        source.parent.mkdir()
        fixture = sources / version / "WorkshopIdentity.vpk"
        shutil.copyfile(fixture, source)
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if digest != FIXTURE_SHA256[version]:
            raise RuntimeError(f"{fixture} SHA-256 is {digest}. Expected {FIXTURE_SHA256[version]}.")
        print(f"{name}: {source.relative_to(out)} SHA-256 {digest}", flush=True)

    phase_dir = out / f"{sequence:02d}-{name}"
    phase_dir.mkdir()
    cache = phase_dir / "Cache"
    env = os.environ.copy()
    for name_to_clear in ("BLENDITY_TEST_FILTER", "BLENDITY_TEST_RANGE", "BLENDITY_TEST_ALSO"):
        env.pop(name_to_clear, None)
    env.update({
        "BLENDITY_IDENTITY_PHASE": name,
        "BLENDITY_IDENTITY_PROJECT": str(out / "Project"),
        "BLENDITY_IDENTITY_BASELINE": str(out / "baseline.txt"),
        "BLENDITY_IDENTITY_ITEM": str(source) if source else "",
        "BLENDITY_IDENTITY_TEXTURE_POLICY": texture_policy,
        "BLENDITY_TEST_DIR": str(phase_dir / "Tests"),
        "BLENDITY_CACHE_DIR": str(cache),
        "BLENDITY_PREFS_DIR": str(phase_dir / "Preferences"),
        "BLENDITY_VIEWPORT_DEVICE": "cpu",
        "BLENDITY_GI_DEVICE": "cpu",
        "BLENDITY_DENOISE_DEVICE": "cpu",
    })
    log = phase_dir / "result.log"
    with log.open("w", encoding="utf-8", newline="\n") as stream:
        ret = subprocess.run([str(exe), "--group", "source_identity"], cwd=repo, env=env,
                             stdout=stream, stderr=subprocess.STDOUT, check=False)
    text = log.read_text(encoding="utf-8", errors="replace")
    print(text, end="", flush=True)
    if source:
        source.parent.rename(out / f"{install_dir}-detached")
        if source.exists():
            raise RuntimeError(f"The previous source is still accessible at {source}")
    if cache.exists():
        cache.rename(phase_dir / "Cache-detached")
        if cache.exists():
            raise RuntimeError(f"The previous conversion cache is still accessible at {cache}")
    if ret.returncode != 0 or "[ ok ] Workshop reimport preserves project identity" not in text:
        raise RuntimeError(f"{name} failed or did not run. See {log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path, help="Blendity test executable with source_identity registered")
    parser.add_argument("--repo", required=True, type=Path, help="repository root for the test runner's working directory")
    parser.add_argument("--fixtures", type=Path,
                        default=Path(__file__).resolve().parent.parent / "identity" / "sources",
                        help="folder containing v1/WorkshopIdentity.vpk and v2/WorkshopIdentity.vpk")
    parser.add_argument("--scratch-parent", type=Path, help="existing parent for a new run folder")
    parser.add_argument("--texture-policy", choices=("preserve", "refresh"), default="preserve",
                        help="keep project PNG bytes, or diagnose source-pixel refresh (default: preserve)")
    args = parser.parse_args()
    exe, repo, sources = (value.resolve(strict=True) for value in (args.exe, args.repo, args.fixtures))
    parent = args.scratch_parent.resolve(strict=True) if args.scratch_parent else None
    out = Path(tempfile.mkdtemp(prefix="workshop-identity-", dir=parent))
    print(f"Identity run output: {out}\nTexture policy: {args.texture_policy}", flush=True)
    try:
        for sequence, phase in enumerate(PHASES, 1):
            run_phase(exe, repo, out, sources, sequence, phase, args.texture_policy)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"All five phases passed. Logs and scratch scene: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
