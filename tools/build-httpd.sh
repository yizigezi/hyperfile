#!/usr/bin/env bash
# Build the standalone HTTP daemon and its tiny QuickJS controller plugin.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INC="$ROOT/native"
TARGET="${1:-armv7}"

case "$TARGET" in
  armv7)
    OUT="$ROOT/native/dist"
    LIBS="$ROOT/libs"
    GLIBC="2.23"
    TARGET_TRIPLE="arm-linux-gnueabihf.$GLIBC"
    EXPECT_MACHINE="ARM"
    ;;
  arm64)
    OUT="$ROOT/native/dist-arm64"
    LIBS="$ROOT/libs"
    GLIBC="2.29"
    TARGET_TRIPLE="aarch64-linux-gnu.$GLIBC"
    EXPECT_MACHINE="AArch64"
    ;;
  *)
    echo "Usage: $0 [armv7|arm64]" >&2
    exit 1
    ;;
esac

if command -v zig >/dev/null 2>&1; then
  CC=(zig cc -target "$TARGET_TRIPLE")
elif python3 -c "import ziglang" >/dev/null 2>&1; then
  CC=(python3 -m ziglang cc -target "$TARGET_TRIPLE")
elif [ "$TARGET" = "armv7" ] && command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
  CC=(arm-linux-gnueabihf-gcc)
else
  echo "zig or the matching cross-GCC is required" >&2
  exit 1
fi

mkdir -p "$OUT" "$LIBS"
echo "== Build standalone hyperfile-httpd ($TARGET) =="
"${CC[@]}" -O2 -g0 -fno-ident -I "$INC" \
  "$ROOT/native/http_server.c" "$ROOT/native/file_manager.c" "$ROOT/native/amr_info.c" \
  "$ROOT/native/amr_update.c" "$ROOT/native/httpd_main.c" \
  -lpthread -o "$OUT/hyperfile-httpd"

echo "== Build standalone self-updater =="
"${CC[@]}" -O2 -g0 -fno-ident -I "$INC" \
  "$ROOT/native/updater_main.c" -o "$OUT/hyperfile-updater"

echo "== Build QuickJS process controller plugin =="
"${CC[@]}" -shared -fPIC -O2 -g0 -fvisibility=hidden \
  -I "$INC" "$ROOT/native/http_controller_plugin.c" \
  -ldl -o "$OUT/libjsapi_hyperfile_http.so"

# Package the daemon, updater, and JSAPI controller as executable resources.
if [ "$TARGET" = "arm64" ]; then
  mkdir -p "$LIBS/arm64" "$LIBS/arm64-orange"
  ARM64_FILES=(
    hyperfile-httpd
    hyperfile-updater
    libjsapi_hyperfile_http.so
  )

  cp "$OUT/hyperfile-httpd" "$LIBS/arm64/libhyperfile-httpd.so"
  cp "$OUT/hyperfile-updater" "$LIBS/arm64/libhyperfile-updater.so"
  cp "$OUT/libjsapi_hyperfile_http.so" "$LIBS/arm64/libjsapi_hyperfile_http.so"
  cp "$OUT/hyperfile-httpd" "$LIBS/arm64-orange/libhyperfile-httpd.so"
  cp "$OUT/hyperfile-updater" "$LIBS/arm64-orange/libhyperfile-updater.so"
  cp "$OUT/libjsapi_hyperfile_http.so" "$LIBS/arm64-orange/libjsapi_hyperfile_http.so"

  chmod 755 "$LIBS/arm64/"*.so "$LIBS/arm64-orange/"*.so
else
  cp "$OUT/hyperfile-httpd" "$LIBS/libhyperfile-httpd.so"
  chmod 755 "$LIBS/libhyperfile-httpd.so"
  cp "$OUT/hyperfile-updater" "$LIBS/libhyperfile-updater.so"
  chmod 755 "$LIBS/libhyperfile-updater.so"
  cp "$OUT/libjsapi_hyperfile_http.so" "$LIBS/libjsapi_hyperfile_http.so"
fi

echo "== Build native local operations JSAPI =="
"${CC[@]}" -shared -fPIC -O2 -g0 -fvisibility=hidden \
  -I "$INC" "$ROOT/native/local_ops_plugin.c" "$ROOT/native/amr_info.c" \
  "$ROOT/native/amr_update.c" \
  -o "$OUT/libjsapi_hyperfile_local_ops.so"
if [ "$TARGET" = "arm64" ]; then
  cp "$OUT/libjsapi_hyperfile_local_ops.so" "$LIBS/arm64/libjsapi_hyperfile_local_ops.so"
  cp "$OUT/libjsapi_hyperfile_local_ops.so" "$LIBS/arm64-orange/libjsapi_hyperfile_local_ops.so"
  chmod 755 "$LIBS/arm64/libjsapi_hyperfile_local_ops.so" "$LIBS/arm64-orange/libjsapi_hyperfile_local_ops.so"
else
  cp "$OUT/libjsapi_hyperfile_local_ops.so" "$LIBS/libjsapi_hyperfile_local_ops.so"
  chmod 755 "$LIBS/libjsapi_hyperfile_local_ops.so"
fi

echo "== ELF checks =="
if [ "$TARGET" = "arm64" ]; then
  CHECK_FILES=(
    "$LIBS/arm64/libhyperfile-httpd.so"
    "$LIBS/arm64/libhyperfile-updater.so"
    "$LIBS/arm64/libjsapi_hyperfile_http.so"
    "$LIBS/arm64/libjsapi_hyperfile_local_ops.so"
  )
else
  CHECK_FILES=(
    "$LIBS/libhyperfile-httpd.so"
    "$LIBS/libhyperfile-updater.so"
    "$LIBS/libjsapi_hyperfile_http.so"
    "$LIBS/libjsapi_hyperfile_local_ops.so"
  )
fi
for f in "${CHECK_FILES[@]}"; do
  machine=$(readelf -h "$f" | awk '/Machine:/ {print $NF}')
  if [ "$machine" != "$EXPECT_MACHINE" ]; then
    echo "ELF machine mismatch: $f expected $EXPECT_MACHINE got $machine" >&2
    exit 1
  fi
  echo "$f: $machine"
  max=$(readelf -V "$f" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 || true)
  echo "$f GLIBC max: ${max:-none}"
  if [ -n "$max" ] && [ "$(printf '%s\n%s\n' "$GLIBC" "${max#GLIBC_}" | sort -V | tail -1)" != "$GLIBC" ]; then
    echo "GLIBC requirement exceeds $GLIBC: $f" >&2
    exit 1
  fi
done
echo "Artifacts copied into libs/."
