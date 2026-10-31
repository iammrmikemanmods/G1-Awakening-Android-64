#!/usr/bin/env bash
# Install the pinned Android build dependencies; requires JDK17+, curl and python3.
set -euo pipefail

g1_project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
g1_sdk_root="${G1_ANDROID_SDK_ROOT:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$g1_project_root/.android-sdk}}}"
g1_ndk_version="28.2.13676358"

if ! command -v javac >/dev/null 2>&1; then
    echo "A JDK is required. Install JDK17 and add its bin directory to PATH." >&2
    exit 1
fi
command -v curl >/dev/null
command -v python3 >/dev/null
mkdir -p "$g1_sdk_root"
g1_sdk_root="$(cd "$g1_sdk_root" && pwd)"

if [[ -x "$g1_sdk_root/cmdline-tools/19.0/bin/sdkmanager" ]]; then
    g1_sdkmanager="$g1_sdk_root/cmdline-tools/19.0/bin/sdkmanager"
elif [[ -x "$g1_sdk_root/cmdline-tools/latest/bin/sdkmanager" ]]; then
    g1_sdkmanager="$g1_sdk_root/cmdline-tools/latest/bin/sdkmanager"
else
    case "$(uname -s)" in
        Linux) g1_archive="commandlinetools-linux-13114758_latest.zip"; g1_sha1="5fdcc763663eefb86a5b8879697aa6088b041e70" ;;
        Darwin) g1_archive="commandlinetools-mac-13114758_latest.zip"; g1_sha1="c3e06a1959762e89167d1cbaa988605f6f7c1d24" ;;
        *) echo "Use Android Studio SDK Manager on this operating system." >&2; exit 1 ;;
    esac
    g1_download_dir="$(mktemp -d)"
    trap 'rm -rf "$g1_download_dir"' EXIT
    curl --fail --location --retry 3 "https://dl.google.com/android/repository/$g1_archive" \
        --output "$g1_download_dir/tools.zip"
    # Checksums are pinned to Google's repository2-3.xml package metadata.
    python3 - "$g1_download_dir/tools.zip" "$g1_sha1" "$g1_sdk_root" <<'PY'
import hashlib, pathlib, shutil, stat, sys, zipfile
archive, expected, sdk = sys.argv[1:]
digest = hashlib.sha1()
with open(archive, 'rb') as source:
    for chunk in iter(lambda: source.read(1024 * 1024), b''):
        digest.update(chunk)
if digest.hexdigest() != expected:
    raise SystemExit('Android command-line tools checksum verification failed.')
destination = pathlib.Path(sdk) / 'cmdline-tools' / '19.0'
with zipfile.ZipFile(archive) as source:
    for member in source.infolist():
        parts = pathlib.PurePosixPath(member.filename).parts
        if not parts or parts[0] != 'cmdline-tools' or '..' in parts:
            raise SystemExit('Unexpected SDK archive path.')
        if len(parts) == 1:
            continue
        target = destination.joinpath(*parts[1:])
        if member.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        mode = member.external_attr >> 16
        if stat.S_ISLNK(mode):
            raise SystemExit('Unexpected symlink in command-line tools.')
        with source.open(member) as inp, target.open('wb') as out:
            shutil.copyfileobj(inp, out)
        if mode:
            target.chmod(mode & 0o777)
PY
    g1_sdkmanager="$g1_sdk_root/cmdline-tools/19.0/bin/sdkmanager"
fi

if [[ "${G1_ACCEPT_ANDROID_LICENSES:-0}" == "1" ]]; then
    (set +o pipefail; yes | "$g1_sdkmanager" --sdk_root="$g1_sdk_root" --licenses >/dev/null)
else
    "$g1_sdkmanager" --sdk_root="$g1_sdk_root" --licenses
fi

"$g1_sdkmanager" --sdk_root="$g1_sdk_root" \
    'platform-tools' 'platforms;android-36' 'build-tools;36.0.0' \
    "ndk;$g1_ndk_version" 'cmake;3.22.1'

python3 - "$g1_project_root/local.properties" "$g1_sdk_root" <<'PY'
import pathlib, sys
path, sdk = sys.argv[1:]
escaped = sdk.replace('\\', '\\\\').replace(':', '\\:').replace(' ', '\\ ')
pathlib.Path(path).write_text('sdk.dir=' + escaped + '\n', encoding='utf-8')
PY

echo "Android toolchain ready: $g1_sdk_root"
echo "Build for Galaxy A36: ./gradlew :app:assembleDebug"
echo "Build for x86_64 emulator: ./gradlew :app:assembleDebug -Pg1Abis=x86_64"
