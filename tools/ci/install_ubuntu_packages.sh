#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# The ubuntu-latest x64 jobs use only packages from Ubuntu main and universe.
set -euo pipefail

installed() {
    [[ $(dpkg-query -W -f='${Status}' "$1" 2>/dev/null) == *' ok installed' ]]
}

missing=()
for package in "$@"; do
    if ! installed "$package"; then
        missing+=("$package")
    fi
done
if (( ${#missing[@]} == 0 )); then
    exit 0
fi

# Ignore optional runner feeds in both modern .sources and legacy .list files.
# Keep apt's archive signature verification and Ubuntu's updates/security pockets.
source /etc/os-release
[[ $ID == ubuntu && $VERSION_CODENAME =~ ^[a-z]+$ ]]
sources=$(mktemp --suffix=.list)
trap 'rm -f "$sources"' EXIT
keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg
cat > "$sources" <<EOF
deb [signed-by=$keyring] https://archive.ubuntu.com/ubuntu $VERSION_CODENAME main universe
deb [signed-by=$keyring] https://archive.ubuntu.com/ubuntu $VERSION_CODENAME-updates main universe
deb [signed-by=$keyring] https://security.ubuntu.com/ubuntu $VERSION_CODENAME-security main universe
EOF
apt_options=(-o "Dir::Etc::sourcelist=$sources" -o Dir::Etc::sourceparts=-)
sudo apt-get "${apt_options[@]}" -o APT::Update::Error-Mode=any --no-list-cleanup update
sudo apt-get "${apt_options[@]}" install -y "${missing[@]}"

for package in "$@"; do
    if ! installed "$package"; then
        echo "Required package is not installed: $package" >&2
        exit 1
    fi
done
