#!/bin/bash
# One round's verification (tools/dev/README.md): the dependency-free tree's builds, the change copied into the
# Blender-libraries clone, its builds, both suites, Linux under WSL, the audits; LMQ=1 the bake-quality section;
# SIDERAIL=1 the SideRail comparison against Unity; POSING=1 the posing sections against the baseline and the three
# real-content posing runs (round 468) - timed: nothing else may build or run meanwhile.
# Usage: TAG=NNN OUT=C:/blsuitesNNN [LMQ=1] [SIDERAIL=1] [POSING=1] bash tools/dev/chain.sh
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
cd "$W" || exit 1
BLENDITY_NO_LTCG=1 cmd //c "$WIN_W\\build.bat release tests" > "$CMP/w_tests_build.log" 2>&1; echo "w tests rc=$?"
BLENDITY_NO_LTCG=1 cmd //c "$WIN_W\\build.bat release stress" > "$CMP/w_stress_build.log" 2>&1; echo "w stress rc=$?"
grep -E " error |LNK[0-9]" "$CMP/w_tests_build.log" "$CMP/w_stress_build.log" | head -5 | grep . && { echo "W BUILD FAILED"; exit 1; }
py "$W/tools/dev/copy_w_to_main.py" > "$CMP/copy.log" 2>&1 || { tail -3 "$CMP/copy.log"; echo "COPY FAILED"; exit 1; }; tail -1 "$CMP/copy.log"
cd "$M" || exit 1
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release tests" > "$CMP/main_tests_build.log" 2>&1; echo "main tests rc=$?"
mv dist/windows/Blendity.exe "dist/windows/Blendity.old$(date +%s).exe" 2>/dev/null  # (a running copy is renamed aside, never closed)
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release app" > "$CMP/main_app_build.log" 2>&1; echo "main app rc=$?"
BLENDITY_NO_LTCG=1 cmd //c "$WIN_M\\build.bat release stress" > "$CMP/main_stress_build.log" 2>&1; echo "main stress rc=$?"
grep -E " error |LNK[0-9]" "$CMP/main_tests_build.log" "$CMP/main_app_build.log" "$CMP/main_stress_build.log" | head -5 | grep . && { echo "MAIN BUILD FAILED"; exit 1; }
cd "$W"
py tools/run_suites.py "$W/dist/windows/blendity_tests.exe" "$M/dist/windows/blendity_tests.exe" --out $OUT 2>&1 | grep -E "checks|FAIL|\[FAIL\]|failed" | grep -v "\[ok\]" | head -20
for d in $OUT/*/; do echo "$d tests: $(cat $d*.log | grep -cE '^\[ (ok|FAIL) \]')"; done
wsl -e bash -lc "cd '$WSL_TREE' && rm -f dist/linux/blendity_tests && ./build.sh release tests > /mnt/c/blcmp$TAG/linux_build.log 2>&1; echo \"linux build rc=\$?\"; grep -E ' error ' /mnt/c/blcmp$TAG/linux_build.log | head -3; test -x dist/linux/blendity_tests && python3 tools/run_suites.py dist/linux/blendity_tests --out /tmp/blsuites$TAG 2>&1 | grep -E 'checks|FAIL|failed' | grep -v '\[ok\]' | head -10"
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
