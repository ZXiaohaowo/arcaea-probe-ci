#!/bin/bash
# build.sh - build the practice frameworks with the standard Apple toolchain.
# Runs on any macOS host with Xcode command line tools (CI runner or a borrowed Mac).
# Usage: ./build.sh [build-id]   (e.g. ./build.sh "$(git rev-parse --short HEAD)")
set -euo pipefail
cd "$(dirname "$0")"

BUILD_ID="${1:-dev}"

echo "==== toolchain info ===="
xcodebuild -version || true
xcrun --sdk iphoneos clang --version | head -n 2
SDK_VER="$(xcrun --sdk iphoneos --show-sdk-version)"
echo "iPhoneOS SDK: ${SDK_VER}"
echo "build id: ${BUILD_ID}"

mkdir -p out/practice_probe_ci.framework out/practice_bootstrap.framework out/practice_clock_probe.framework

echo "==== build probe ===="
xcrun --sdk iphoneos clang \
  -arch arm64 \
  -mios-version-min=15.0 \
  -dynamiclib -O2 \
  -install_name @executable_path/Frameworks/practice_probe_ci.framework/practice_probe_ci \
  -o out/practice_probe_ci.framework/practice_probe_ci \
  probe.c
cp Info.plist out/practice_probe_ci.framework/Info.plist

echo "==== build bootstrap (build id: ${BUILD_ID}) ===="
xcrun --sdk iphoneos clang \
  -arch arm64 \
  -mios-version-min=15.0 \
  -dynamiclib -O2 \
  "-DPRACTICE_BUILD_ID=\"${BUILD_ID}\"" \
  -install_name @executable_path/Frameworks/practice_bootstrap.framework/practice_bootstrap \
  -o out/practice_bootstrap.framework/practice_bootstrap \
  practice_bootstrap.c
cp bootstrap-Info.plist out/practice_bootstrap.framework/Info.plist

echo "==== build clock probe (build id: ${BUILD_ID}) ===="
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=15.0 -fobjc-arc -O2 \
  -c practice_rate_ui.m -o out/practice_rate_ui.o
xcrun --sdk iphoneos clang \
  -arch arm64 \
  -mios-version-min=15.0 \
  -std=gnu11 \
  -dynamiclib -O2 \
  "-DPRACTICE_BUILD_ID=\"${BUILD_ID}\"" \
  -framework CoreFoundation \
  -framework UIKit -framework Foundation \
  -install_name @executable_path/Frameworks/practice_clock_probe.framework/practice_clock_probe \
  -o out/practice_clock_probe.framework/practice_clock_probe \
  practice_clock_probe.c practice_obs_queue.c practice_time.c practice_shadow.c practice_rate_bias.c practice_rate_session.c out/practice_rate_ui.o
cp clock_probe-Info.plist out/practice_clock_probe.framework/Info.plist

echo "==== verify ===="
for f in out/practice_probe_ci.framework/practice_probe_ci out/practice_bootstrap.framework/practice_bootstrap out/practice_clock_probe.framework/practice_clock_probe; do
  lipo -info "$f"
  xcrun vtool -show-build "$f" || true
  xcrun otool -L "$f" || true
  shasum -a 256 "$f"
done

echo "==== package ===="
( cd out && zip -qry ../practice_probe_ci.framework.zip practice_probe_ci.framework \
  && zip -qry ../practice_bootstrap.framework.zip practice_bootstrap.framework \
  && zip -qry ../practice_clock_probe.framework.zip practice_clock_probe.framework )

echo "==== done ===="
