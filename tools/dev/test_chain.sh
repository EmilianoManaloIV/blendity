#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Exercise the chain without launching Windows build tools, Python suites or WSL.
set -eu
chain=${1:-$(dirname "$0")/chain.sh}
chain=$(cd "$(dirname "$chain")" && pwd)/$(basename "$chain")
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
export FIXTURE=$(cd "$scratch" && pwd)
cd "$FIXTURE"
mkdir -p bin 'candidate tree/dist/windows' 'library tree/dist/windows' 'linux tree/dist/linux' 'Windows bundle/tbb/include' 'blender/lib/windows_x64/tbb/include'
mkdir -p 'Windows bundle/tbb/lib' 'blender/lib/windows_x64/tbb/lib' 'incomplete bundle/tbb/include'
touch 'Windows bundle/tbb/lib/tbb12.lib' 'blender/lib/windows_x64/tbb/lib/tbb12.lib'
export BL_W="$FIXTURE/candidate tree" BL_MAIN="$FIXTURE/library tree"
export BL_WSL_TREE="$FIXTURE/linux tree" OUT="$FIXTURE/suite logs" BL_CHAIN_LOGS="$FIXTURE/compare logs"
export PATH="$FIXTURE/bin:$PATH"
unset TEST_GROUPS LMQ SIDERAIL POSING MSYS_NO_PATHCONV
export BLENDITY_LIBDIR="$FIXTURE/Windows bundle" BLENDITY_NO_LIBS=1 MSYS2_ARG_CONV_EXCL='*'

cat > bin/cmd <<'EOF'
#!/bin/bash
[[ $# == 5 && $1 == //c && $2 == call && $3 == *build.bat && $4 == release ]] || exit 92
[[ $CASE != windows-build || $5 != tests ]] || exit 23
[[ -z $MSYS2_ARG_CONV_EXCL ]] || exit 93
if [[ $PWD == "$BL_W" ]]; then
  [[ $BLENDITY_NO_LIBS == 1 && -z $BLENDITY_LIBDIR ]] || exit 94
  [[ $CASE == no-build-mode ]] || echo '[blendity] Dependency-free build'
else
  [[ $PWD == "$BL_MAIN" && -z $BLENDITY_NO_LIBS ]] || exit 95
  bundle=$(cygpath -u "$BLENDITY_LIBDIR" 2>/dev/null || echo "$BLENDITY_LIBDIR")
  [[ -d "$bundle/tbb/include" && -f "$bundle/tbb/lib/tbb12.lib" ]] || exit 96
  if [[ $CASE == wrong-library-mode ]]; then
    echo '[blendity] Dependency-free build'
  else
    echo '[blendity] Using Blender libraries from fixture'
  fi
fi
EOF
cat > bin/py <<'EOF'
#!/bin/bash
[[ -z $MSYS2_ARG_CONV_EXCL ]] || exit 98
[[ $CASE != windows-suite || $1 != tools/run_suites.py ]] || exit 24
[[ $CASE != audit || $1 != tools/hci_klm.py ]] || exit 25
EOF
cat > bin/python3 <<'EOF'
#!/bin/bash
[[ $CASE != linux-suite ]] || exit 26
EOF
cat > bin/taskset <<'EOF'
#!/bin/bash
[[ $# == 5 && $1 == -c && $2 == 0-9 && $3 == ./build.sh && $4 == release && $5 == tests ]] || exit 97
shift 2
exec "$@"
EOF
cat > bin/wsl <<'EOF'
#!/bin/bash
[[ $CASE != wsl ]] || exit 27
[[ $# == 4 && $1 == -e && $2 == bash && $3 == -lc ]] || exit 91
# Execute the payload in the host Bash, remapping only Linux scratch paths.
payload=${4//\/tmp\/blsuites/$FIXTURE\/linux-suites}
payload=${payload//\/mnt\/c\/blcmp/$FIXTURE\/legacy-cmp}
bash -c "$payload"
EOF
cat > tests <<'EOF'
#!/bin/bash
if [[ $1 == --list-groups ]]; then
  printf 'mesh\neditor\n'
  [[ $CASE != discovery ]] || exit 28
  exit 0
fi
printf '%s %s\n' "$PWD" "$2" >> "$FIXTURE/groups"
if [[ $2 == mesh ]]; then
  [[ $CASE != windows-group || $PWD != "$BL_W" ]] || exit 29
  [[ $CASE != linux-group || $PWD != "$BL_WSL_TREE" ]] || exit 30
  [[ $CASE != no-summary ]] || exit 0
fi
echo '1 checks, 0 failed'
EOF
cat > 'linux tree/build.sh' <<'EOF'
#!/bin/bash
[[ $CASE != linux-build ]] || exit 31
[[ $CASE != missing-linux-binary ]] || exit 0
cp "$FIXTURE/tests" dist/linux/blendity_tests
EOF
chmod +x bin/* tests 'linux tree/build.sh'
cp tests 'candidate tree/dist/windows/blendity_tests.exe'
cp tests 'library tree/dist/windows/blendity_tests.exe'

check() {
  local expected=$1 diagnostic=$2 groups=${3-mesh,mesh} rc=0
  export CASE=$expected TEST_GROUPS=$groups
  rm -f groups
  bash "$chain" > chain.log 2>&1 || rc=$?
  if [[ -z $diagnostic ]]; then
    [[ $rc == 0 ]] && grep -q '^CHAIN DONE$' chain.log || { cat chain.log; return 1; }
  else
    [[ $rc != 0 ]] && grep -Fq "$diagnostic" chain.log && ! grep -q '^CHAIN DONE$' chain.log ||
      { echo "$CASE: expected failure ($diagnostic), got rc=$rc"; cat chain.log; return 1; }
  fi
  echo "$CASE: rc=$rc"
}

check success ''
[[ $(wc -l < groups) == 6 ]]
for tree in "$BL_W" "$BL_MAIN" "$BL_WSL_TREE"; do
  [[ $(grep -Fxc "$tree mesh" groups) == 1 && $(grep -Fxc "$tree editor" groups) == 1 ]]
done
check full-success '' ''
check windows-group 'GROUP FAILED: w mesh'
[[ $(wc -l < groups) == 6 ]]
check linux-group 'GROUP FAILED: linux mesh'
[[ $(wc -l < groups) == 6 ]]
check no-summary 'GROUP FAILED: w mesh'
check windows-build 'w tests failed (rc=23'
check discovery 'list test groups failed (rc=28'
check wsl 'Linux verification failed (rc=27'
check linux-build 'linux build failed (rc=31'
check missing-linux-binary 'Linux test executable missing'
check windows-suite 'Windows suites failed (rc=24' ''
check linux-suite 'Linux suite failed (rc=26' ''
check audit 'tools/hci_klm.py failed (rc=25'
check no-build-mode 'w tests did not confirm its build mode'
check wrong-library-mode 'main tests did not confirm its build mode'
BLENDITY_LIBDIR="$FIXTURE/missing bundle" check missing-bundle 'Windows Blender libraries missing'
BLENDITY_LIBDIR= check adjacent-bundle ''
BLENDITY_LIBDIR="$FIXTURE/incomplete bundle" check incomplete-bundle 'Windows Blender libraries missing'
