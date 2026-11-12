#!/bin/bash
# One round's verification (tools/dev/README.md): the dependency-free tree's builds, the change copied into the
# Blender-libraries clone, its builds, both suites, Linux under WSL, the audits; LMQ=1 the bake-quality section;
# SIDERAIL=1 the SideRail comparison against Unity; POSING=1 the posing sections against the baseline and the three
# real-content posing runs (round 468) - timed: nothing else may build or run meanwhile.
# TEST_GROUPS="a,b" (round 493, ADR 0134) runs only those test groups and `editor` in the three suites - the round's
# own groups; CI runs the whole suite. Without it everything runs, as before. (Not GROUPS: bash keeps that name for
# the user's group ids and throws a value given to it away.)
# Usage: TAG=NNN OUT=C:/blsuitesNNN [TEST_GROUPS=a,b] [LMQ=1] [SIDERAIL=1] [POSING=1] bash tools/dev/chain.sh
W=${BL_W:-W:/blendity}
M=${BL_MAIN:-C:/Users/trick/Downloads/blender-main/blendity}
WSL_TREE=${BL_WSL_TREE:-/mnt/c/Users/trick/AppData/Local/Temp/claude/C--Users-trick-Downloads-blender-main/a08cffd5-528d-4068-9221-b120acf3ea14/scratchpad/wt169/blendity}
U=${BL_UNITY:-"C:/Program Files/Unity/Hub/Editor/2022.3.22f1/Editor/Unity.exe"}
BP=${BL_SIDERAIL:-"C:/Users/trick/Downloads/SideRail Blendity"}
TAG=${TAG:-x}
OUT=${OUT:-C:/blsuites$TAG}
CMP=C:/blcmp$TAG  # (logs here, never the drive's root: a failed redirect there means the chain never runs)
mkdir -p $CMP
WIN_W=$(cygpath -w "$W" 2>/dev/null || echo "$W")
WIN_M=$(cygpath -w "$M" 2>/dev/null || echo "$M")
TEST_GROUPS=${TEST_GROUPS:-}
if [ -n "$TEST_GROUPS" ]; then
  # Before anything is built: a list that is no list of names stops here, not after three builds.
  echo "$TEST_GROUPS" | grep -qE '^[a-z][a-z0-9_]*(,[a-z][a-z0-9_]*)*$' || { echo "TEST_GROUPS is not a list of group names (a,b,c): '$TEST_GROUPS'"; exit 1; }
  TEST_GROUPS=$(echo "$TEST_GROUPS" | tr ',' '\n' | awk '!seen[$0]++' | paste -sd, -)  # (a name given twice runs once)
  case ",$TEST_GROUPS," in *,editor,*) ;; *) TEST_GROUPS="$TEST_GROUPS,editor" ;; esac  # (CLAUDE.md: the round's groups and editor)
  echo "test groups: $TEST_GROUPS (the whole suite runs on CI)"
fi

# One build's named groups, one after another, each in its own process and test folder (as tools/run_suites.py runs
# them; one at a time, so the same group of two builds never runs at once: Live Link's tests take fixed ports).
run_groups() {  # <blendity_tests> <label> <its tree>
  local exe=$1 label=$2 tree=$3 g rc dir
  mkdir -p "$OUT/$label"
  rm -f "$OUT/$label"/*.log  # (an earlier group run's logs would be counted with these)
  for g in ${TEST_GROUPS//,/ }; do
    dir="$OUT/$label/$g"
    rm -rf "$dir"; mkdir -p "$dir"
    (cd "$tree" && BLENDITY_TEST_DIR="$dir" BLENDITY_CACHE_DIR="$OUT/$label/_cache" "$exe" --group "$g" > "$OUT/$label/$g.log" 2>&1); rc=$?
    echo "  [$label] $g: $(grep -E '^[0-9]+ checks, [0-9]+ failed' "$OUT/$label/$g.log" | tail -1) rc=$rc"
    grep -E '^\[FAIL\]|^  FAIL ' "$OUT/$label/$g.log" | head -10
    # A crash, a name the program does not know (exit 2), a run without its summary line: said, never passed over.
    if [ $rc -ne 0 ] || ! grep -qE '^[1-9][0-9]* checks, 0 failed' "$OUT/$label/$g.log"; then echo "GROUP FAILED: $label $g (rc=$rc, $OUT/$label/$g.log)"; fi
  done
}

cd "$W" || exit 1
BLENDITY_NO_LTCG=1 cmd //c "$WIN_W\\build.bat release tests" > "$CMP/w_tests_build.log" 2>&1; echo "w tests rc=$?"
BLENDITY_NO_LTCG=1 cmd //c "$WIN_W\\build.bat release stress" > "$CMP/w_stress_build.log" 2>&1; echo "w stress rc=$?"
grep -E " error |LNK[0-9]" "$CMP/w_tests_build.log" "$CMP/w_stress_build.log" | head -5 | grep . && { echo "W BUILD FAILED"; exit 1; }
if [ -n "$TEST_GROUPS" ]; then
  # A name that is no group stops the chain here, before the copy and the second tree's builds.
  for g in ${TEST_GROUPS//,/ }; do
    "$W/dist/windows/blendity_tests.exe" --list-groups | tr -d '\r' | grep -qx "$g" || { echo "TEST_GROUPS: no such group: $g (blendity_tests --list-groups names them)"; exit 1; }
  done
fi
py "$W/tools/dev/copy_w_to_main.py" > "$CMP/copy.log" 2>&1 || { tail -3 "$CMP/copy.log"; echo "COPY FAILED"; exit 1; }; tail -1 "$CMP/copy.log"
cd "$M" || exit 1
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release tests" > "$CMP/main_tests_build.log" 2>&1; echo "main tests rc=$?"
mv dist/windows/Blendity.exe "dist/windows/Blendity.old$(date +%s).exe" 2>/dev/null  # (a running copy is renamed aside, never closed)
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release app" > "$CMP/main_app_build.log" 2>&1; echo "main app rc=$?"
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release stress" > "$CMP/main_stress_build.log" 2>&1; echo "main stress rc=$?"
grep -E " error |LNK[0-9]" "$CMP/main_tests_build.log" "$CMP/main_app_build.log" "$CMP/main_stress_build.log" | head -5 | grep . && { echo "MAIN BUILD FAILED"; exit 1; }
cd "$W"
if [ -n "$TEST_GROUPS" ]; then
  run_groups "$W/dist/windows/blendity_tests.exe" w "$W"
  run_groups "$M/dist/windows/blendity_tests.exe" main "$M"
  LINUX_RUN="for g in ${TEST_GROUPS//,/ }; do d=/tmp/blsuites$TAG/linux/\$g; rm -rf \$d; mkdir -p \$d; BLENDITY_TEST_DIR=\$d BLENDITY_CACHE_DIR=/tmp/blsuites$TAG/linux/_cache dist/linux/blendity_tests --group \$g > \$d.log 2>&1; rc=\$?; echo \"  [linux] \$g: \$(grep -E '^[0-9]+ checks, [0-9]+ failed' \$d.log | tail -1) rc=\$rc\"; grep -E '^\[FAIL\]|^  FAIL ' \$d.log | head -10; if [ \$rc -ne 0 ] || ! grep -qE '^[1-9][0-9]* checks, 0 failed' \$d.log; then echo \"GROUP FAILED: linux \$g (rc=\$rc)\"; fi; done"
else
  py tools/run_suites.py "$W/dist/windows/blendity_tests.exe" "$M/dist/windows/blendity_tests.exe" --out $OUT 2>&1 | grep -E "checks|FAIL|\[FAIL\]|failed" | grep -v "\[ok\]" | head -20
  LINUX_RUN="python3 tools/run_suites.py dist/linux/blendity_tests --out /tmp/blsuites$TAG 2>&1 | grep -E 'checks|FAIL|failed' | grep -v '\[ok\]' | head -10"
fi
for d in $OUT/*/; do echo "$d tests: $(cat $d*.log | grep -cE '^\[ (ok|FAIL) \]')"; done
wsl -e bash -lc "cd '$WSL_TREE' && rm -f dist/linux/blendity_tests && ./build.sh release tests > /mnt/c/blcmp$TAG/linux_build.log 2>&1; echo \"linux build rc=\$?\"; grep -E ' error ' /mnt/c/blcmp$TAG/linux_build.log | head -3; test -x dist/linux/blendity_tests && { $LINUX_RUN; }"
echo SUITES DONE
echo "--- audits"
py tools/test_coverage_map.py --check --widgets 2>&1 | tail -2
py tools/hci_consistency_audit.py --check 2>&1 | tail -2
py tools/hci_klm.py --check 2>&1 | tail -1
py tools/unity_meta.py --check 2>&1 | tail -1
py tools/make_books.py --check-cited 2>&1 | tail -1
py tools/sweep_commands.py --check 2>&1 | tail -1
py tools/test_perf_suite.py 2>&1 | tail -1
py tools/bench_world.py --self-test 2>&1 | tail -1
echo AUDITS DONE
if [ -n "$LMQ" ]; then
  (cd "$M/dist/windows" && ./blendity_stress.exe --only lightmap_quality --out $CMP/lmq > /dev/null 2>&1; echo "lmq rc=$?"; sed -n "/Bake quality/,/^---/p" $CMP/lmq/latest.md)
fi
if [ -n "$SIDERAIL" ]; then
  ST="$W/tools/dev/siderail_stations.json"
  BA="Assets/Imported/SideRail_Ver1.0.0/QuickBrown/SideRail/SCENE/SideRail.unity"
  py tools/unity_compare.py world "$U" C:/blsiderail/world Assets/QuickBrown/SideRail/SCENE/SideRail.unity "$BP" "$BA" "$ST" $CMP/blendity_bake --blendity "$M/dist/windows/Blendity.exe" --blendity-bake --pages --noise > $CMP/bb.log 2>&1; echo "blendity bake rc=$?"
  cat $CMP/blendity_bake/report.md
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
  py tools/perf_suite.py --stress "$M/dist/windows/blendity_stress.exe" --blendity "$EXE" --sections posing,animation --out $CMP/perf --check > $CMP/posing_check.log 2>&1; rc=$?  # (its own status, not tail's)
  tail -40 $CMP/posing_check.log; echo "posing sections rc=$rc"
  py tools/bench_world.py --exe "$EXE" --project "$SH" --prefab "$PREFAB" --camera "$CAM 0 0.9 0" --posing --no-pipeline --out $CMP/posing_avatar > $CMP/posing_avatar.log 2>&1; echo "avatar alone rc=$?"
  py tools/bench_world.py --exe "$EXE" --project "$BP" --scene "$BA" --camera "$CAM 3 0.9 4.805" --prop "$PROP" --posing --no-pipeline --out $CMP/posing_world > $CMP/posing_world.log 2>&1; echo "world alone rc=$?"
  py tools/bench_world.py --exe "$EXE" --project "$BP" --scene "$BA" --avatar-package "$SHP" --avatar-prefab "$PREFAB" --avatar-at "$AT" --camera "$CAM 3 0.9 4.805" --prop "$PROP" --posing --settings --no-pipeline --out $CMP/posing_avatar_in_world > $CMP/posing_avatar_in_world.log 2>&1; echo "avatar in the world rc=$?"
  for r in posing_avatar posing_world posing_avatar_in_world; do sed -n "/## Posing/,\$p" $CMP/$r/report.md; done
fi
echo CHAIN DONE
