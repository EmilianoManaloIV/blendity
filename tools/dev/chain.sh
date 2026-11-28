#!/bin/bash
# One round's verification: Windows builds and suites, Linux under WSL, and audits.
# TEST_GROUPS=a,b runs those groups plus editor in each suite (ADR 0134).
# Without TEST_GROUPS, run every group. Optional LMQ, SIDERAIL and POSING stages
# require a hub reservation under docs/handoff/MACHINE.md.
# From Git Bash, with the hub's build reservations:
# BL_W=C:/candidate BL_MAIN=C:/libraries BL_WSL_TREE=/mnt/c/candidate \
#   BLENDITY_LIBDIR=C:/bundle/windows_x64 TAG=NNN OUT=C:/blsuitesNNN \
#   TEST_GROUPS=modeling,editor bash tools/dev/chain.sh
W=${BL_W:-W:/blendity}
M=${BL_MAIN:?set BL_MAIN to the scratch Blender-libraries checkout}
WSL_TREE=${BL_WSL_TREE:?set BL_WSL_TREE to the candidate scratch checkout as seen by WSL}
U=${BL_UNITY:-"C:/Program Files/Unity/Hub/Editor/2022.3.22f1/Editor/Unity.exe"}
BP=${BL_SIDERAIL:-"C:/Users/trick/Downloads/SideRail Blendity"}
TAG=${TAG:-x}
OUT=${OUT:-C:/blsuites$TAG}
CMP=${BL_CHAIN_LOGS:-C:/blcmp$TAG}

if [[ -v MSYS_NO_PATHCONV ]]; then
  echo 'Unset MSYS_NO_PATHCONV before running the Windows builds from Git Bash.' >&2
  exit 1
fi
# Windows tools need Git Bash path conversion. The WSL call opts out below.
unset MSYS2_ARG_CONV_EXCL
if [[ ! $TAG =~ ^[a-zA-Z0-9_-]+$ ]]; then
  echo "TAG must be a simple name: '$TAG'" >&2
  exit 1
fi
W=$(cd "$W" && pwd) || exit 1
M=$(cd "$M" && pwd) || exit 1
LIBDIR=${BLENDITY_LIBDIR:-"$M/../blender/lib/windows_x64"}
LIBDIR=$(cygpath -u "$LIBDIR" 2>/dev/null || echo "$LIBDIR")
# Check both inputs build.bat uses for the first bundled dependency.
if [ ! -d "$LIBDIR/tbb/include" ] || [ ! -f "$LIBDIR/tbb/lib/tbb12.lib" ]; then
  echo "Windows Blender libraries missing at $LIBDIR. Set BLENDITY_LIBDIR to the Windows bundle with tbb/include and tbb/lib/tbb12.lib." >&2
  exit 1
fi
WIN_LIBDIR=$(cygpath -w "$LIBDIR" 2>/dev/null || echo "$LIBDIR")
mkdir -p "$OUT" "$CMP" || exit 1
OUT=$(cd "$OUT" && pwd) || exit 1
CMP=$(cd "$CMP" && pwd) || exit 1
WIN_W=$(cygpath -w "$W" 2>/dev/null || echo "$W")
WIN_M=$(cygpath -w "$M" 2>/dev/null || echo "$M")
TEST_GROUPS=${TEST_GROUPS:-}
if [ -n "$TEST_GROUPS" ]; then
  [[ $TEST_GROUPS =~ ^[a-z][a-z0-9_]*(,[a-z][a-z0-9_]*)*$ ]] ||
    { echo "TEST_GROUPS is not a list of group names (a,b,c): '$TEST_GROUPS'"; exit 1; }
  TEST_GROUPS=$(echo "$TEST_GROUPS" | tr ',' '\n' | awk '!seen[$0]++' | paste -sd, -)
  case ",$TEST_GROUPS," in *,editor,*) ;; *) TEST_GROUPS="$TEST_GROUPS,editor" ;; esac
  echo "test groups: $TEST_GROUPS (the whole suite runs on CI)"
fi

# Check the producer before printing a bounded excerpt from its log.
logged() {  # <stage> <log path> <excerpt lines> <command>...
  local stage=$1 log=$2 lines=$3 rc=0
  shift 3
  "$@" > "$log" 2>&1 || rc=$?
  tail -n "$lines" "$log"
  if [ "$rc" -ne 0 ]; then
    echo "$stage failed (rc=$rc, $log)" >&2
  fi
  return "$rc"
}

windows_build() {
  local label=$1 tree=$2 target=$3 log="$CMP/${1}_${3}_build.log"
  local no_libs=1 libdir= mode='Dependency-free build'
  if [ "$label" = main ]; then
    no_libs=
    libdir=$WIN_LIBDIR
    mode='Using Blender libraries'
  fi
  BLENDITY_NO_LTCG=1 BLENDITY_NO_LIBS=$no_libs BLENDITY_LIBDIR=$libdir \
    logged "$label $target" "$log" 5 cmd //c call "$tree\\build.bat" release "$target" || return "$?"
  if ! grep -Fq "[blendity] $mode" "$log"; then
    echo "$label $target did not confirm its build mode: $mode ($log)" >&2
    return 1
  fi
  # Keep the compiler-log check as well as checking the batch file's status.
  if grep -qE " error |LNK[0-9]" "$log"; then
    echo "$label $target build reported errors ($log)" >&2
    return 1
  fi
}

# One process and scratch folder per group. Keep testing after a failed group.
run_groups() {  # <exe> <label> <tree> <output root> <group list>
  local exe=$1 label=$2 tree=$3 out="$4/$2" groups=$5 g rc dir failed=0
  mkdir -p "$out" && rm -f "$out"/*.log || return 1
  for g in ${groups//,/ }; do
    dir="$out/$g"
    rc=0
    (cd "$tree" && rm -rf "$dir" && mkdir -p "$dir" &&
      BLENDITY_TEST_DIR="$dir" BLENDITY_CACHE_DIR="$out/_cache" "$exe" --group "$g") > "$dir.log" 2>&1 || rc=$?
    echo "  [$label] $g: $(grep -E '^[0-9]+ checks, [0-9]+ failed' "$dir.log" | tail -1) rc=$rc"
    grep -E '^\[FAIL\]|^  FAIL ' "$dir.log" | head -10
    if [ "$rc" -ne 0 ] || ! grep -qE '^[1-9][0-9]* checks, 0 failed([[:space:]]|$)' "$dir.log"; then
      echo "GROUP FAILED: $label $g (rc=$rc, $dir.log)" >&2
      failed=1
    fi
  done
  return "$failed"
}

linux_verify() {
  local tree=$1 out=$2 groups=$3 rc=0
  cd "$tree" || { echo "Cannot enter WSL tree: $tree" >&2; return 1; }
  rm -f dist/linux/blendity_tests || return 1
  taskset -c 0-9 ./build.sh release tests || rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "linux build failed (rc=$rc)" >&2
    return "$rc"
  fi
  if [ ! -x dist/linux/blendity_tests ]; then
    echo 'Linux test executable missing or not executable' >&2
    return 1
  fi
  if [ -n "$groups" ]; then
    run_groups "$PWD/dist/linux/blendity_tests" linux "$PWD" "$out" "$groups"
    return "$?"
  fi
  python3 tools/run_suites.py dist/linux/blendity_tests --out "$out" || {
    rc=$?
    echo "Linux suite failed (rc=$rc)" >&2
    return "$rc"
  }
}

cd "$W" || exit 1
windows_build w "$WIN_W" tests || exit "$?"
windows_build w "$WIN_W" stress || exit "$?"
if [ -n "$TEST_GROUPS" ]; then
  listed=$("$W/dist/windows/blendity_tests.exe" --list-groups) || {
    rc=$?
    echo "list test groups failed (rc=$rc)" >&2
    exit "$rc"
  }
  for g in ${TEST_GROUPS//,/ }; do
    if ! grep -qx "$g" <<< "${listed//$'\r'/}"; then
      echo "TEST_GROUPS: no such group: $g (blendity_tests --list-groups names them)" >&2
      exit 1
    fi
  done
fi
BL_W="$WIN_W" BL_MAIN="$WIN_M" logged copy "$CMP/copy.log" 3 py "$W/tools/dev/copy_w_to_main.py" || exit "$?"
cd "$M" || exit 1
windows_build main "$WIN_M" tests || exit "$?"
# A running copy is renamed aside. A first build has no executable to rename.
if [ -e dist/windows/Blendity.exe ]; then
  mv dist/windows/Blendity.exe "dist/windows/Blendity.old$(date +%s).exe" || exit 1
fi
windows_build main "$WIN_M" app || exit "$?"
windows_build main "$WIN_M" stress || exit "$?"

cd "$W" || exit 1
failed=0
if [ -n "$TEST_GROUPS" ]; then
  run_groups "$W/dist/windows/blendity_tests.exe" w "$W" "$OUT" "$TEST_GROUPS" || failed=1
  run_groups "$M/dist/windows/blendity_tests.exe" main "$M" "$OUT" "$TEST_GROUPS" || failed=1
else
  logged 'Windows suites' "$CMP/windows_suites.log" 20 py tools/run_suites.py \
    "$W/dist/windows/blendity_tests.exe" "$M/dist/windows/blendity_tests.exe" --out "$OUT" || failed=1
fi
# Send one Bash command with separately shell-quoted arguments. Git Bash must
# leave the Linux paths in it alone when launching Windows' wsl.exe.
printf -v linux_args '%q ' "$WSL_TREE" "/tmp/blsuites$TAG" "$TEST_GROUPS"
linux_command="$(declare -f run_groups linux_verify); linux_verify $linux_args"
MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 logged 'Linux verification' "$CMP/linux.log" 30 \
  wsl -e bash -lc "$linux_command" || {
    grep -E '^GROUP FAILED:' "$CMP/linux.log"
    failed=1
  }

echo "--- audits"
for audit in test_coverage_map.py hci_consistency_audit.py hci_klm.py unity_meta.py make_books.py \
    test_sweep_commands.py sweep_commands.py test_perf_suite.py bench_world.py; do
  case "$audit" in
    test_coverage_map.py) args=(--check --widgets) ;;
    make_books.py) args=(--check-cited) ;;
    bench_world.py) args=(--self-test) ;;
    test_*) args=() ;;
    *) args=(--check) ;;
  esac
  logged "tools/$audit" "$CMP/$audit.log" 4 py "tools/$audit" "${args[@]}" || failed=1
done
if [ -n "$LMQ" ]; then
  (cd "$M/dist/windows" && logged lmq "$CMP/lmq.log" 10 ./blendity_stress.exe --only lightmap_quality --out "$CMP/lmq" && sed -n "/Bake quality/,/^---/p" "$CMP/lmq/latest.md") || failed=1
fi
if [ -n "$SIDERAIL" ]; then
  ST="$W/tools/dev/siderail_stations.json"
  BA="Assets/Imported/SideRail_Ver1.0.0/QuickBrown/SideRail/SCENE/SideRail.unity"
  logged "blendity bake" "$CMP/bb.log" 10 py tools/unity_compare.py world "$U" C:/blsiderail/world Assets/QuickBrown/SideRail/SCENE/SideRail.unity "$BP" "$BA" "$ST" "$CMP/blendity_bake" --blendity "$M/dist/windows/Blendity.exe" --blendity-bake --pages --noise && cat "$CMP/blendity_bake/report.md" || failed=1

fi
if [ -n "$POSING" ]; then
  # Round 468 (task 0444): posing, scrubbing and playing an avatar. The stress sections against the committed baseline
  # (a row over its budget in the baseline too is "still over", not a failure), then the owner's avatar alone, their
  # world alone, and the avatar standing in the world - each on a copy the tool makes under $CMP, never the projects.
  SH=${BL_SHINANO:-C:/bltest399/default_project}
  SHP=${BL_SHINANO_PACKAGE:-C:/Users/trick/Downloads/Shinano_ver1.02/Shinano_ver1.02/Shinano.unitypackage}
  PREFAB="Assets/Imported/Shinano/Shinano/Prefab/Shinano.prefab"
  BA="Assets/Imported/SideRail_Ver1.0.0/QuickBrown/SideRail/SCENE/SideRail.unity"
  EXE="$M/dist/windows/Blendity.exe"
  # Round 468's view: the Scene view's camera 3.2 m from the avatar's chest, looking down the tunnel - the same view of
  # the avatar alone (at the origin) and standing on SideRail's platform; a prop of the world for the "one object
  # moved" row.
  CAM="270 10 3.2"
  AT="3 0 4.805"
  PROP=${BL_POSING_PROP:-Stool}
  logged "posing sections" "$CMP/posing_check.log" 40 py tools/perf_suite.py --stress "$M/dist/windows/blendity_stress.exe" --blendity "$EXE" --sections posing,animation --out "$CMP/perf" --check || failed=1
  logged "avatar alone" "$CMP/posing_avatar.log" 10 py tools/bench_world.py --exe "$EXE" --project "$SH" --prefab "$PREFAB" --camera "$CAM 0 0.9 0" --posing --no-pipeline --out "$CMP/posing_avatar" || failed=1
  logged "world alone" "$CMP/posing_world.log" 10 py tools/bench_world.py --exe "$EXE" --project "$BP" --scene "$BA" --camera "$CAM 3 0.9 4.805" --prop "$PROP" --posing --no-pipeline --out "$CMP/posing_world" || failed=1
  logged "avatar in the world" "$CMP/posing_avatar_in_world.log" 10 py tools/bench_world.py --exe "$EXE" --project "$BP" --scene "$BA" --avatar-package "$SHP" --avatar-prefab "$PREFAB" --camera "$CAM 3 0.9 4.805" --prop "$PROP" --posing --settings --no-pipeline --out "$CMP/posing_avatar_in_world" || failed=1
  for r in posing_avatar posing_world posing_avatar_in_world; do
    sed -n "/## Posing/,\$p" "$CMP/$r/report.md" || failed=1
  done
fi
if [ "$failed" -ne 0 ]; then
  echo "CHAIN FAILED (logs: $CMP)" >&2
  exit 1
fi
echo CHAIN DONE
