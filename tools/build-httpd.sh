#!/usr/bin/env bash
# Build the standalone HTTP daemon and its tiny QuickJS controller plugin.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INC="$ROOT/native"
OUT="$ROOT/native/dist"
LIBS="$ROOT/libs"
GLIBC="2.23"

if command -v zig >/dev/null 2>&1; then
  CC=(zig cc -target arm-linux-gnueabihf.$GLIBC)
elif python3 -c "import ziglang" >/dev/null 2>&1; then
  CC=(python3 -m ziglang cc -target arm-linux-gnueabihf.$GLIBC)
elif command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
  CC=(arm-linux-gnueabihf-gcc)
else
  echo "zig or arm-linux-gnueabihf-gcc is required" >&2
  exit 1
fi

mkdir -p "$OUT" "$LIBS"
echo "== Build standalone hyperfile-httpd =="
"${CC[@]}" -O2 -g0 -fno-ident -I "$INC" \
  "$ROOT/native/http_server.c" "$ROOT/native/httpd_main.c" \
  -lpthread -o "$OUT/hyperfile-httpd"

echo "== Build QuickJS process controller plugin =="
"${CC[@]}" -shared -fPIC -O2 -g0 -fvisibility=hidden \
  -I "$INC" "$ROOT/native/http_controller_plugin.c" \
  -ldl -o "$OUT/libjsapi_hyperfile_http.so"

# Keep both artifacts together: controller resolves the daemon beside its own .so.
cp "$OUT/hyperfile-httpd" "$LIBS/hyperfile-httpd"
chmod 755 "$LIBS/hyperfile-httpd"
cp "$OUT/libjsapi_hyperfile_http.so" "$LIBS/libjsapi_hyperfile_http.so"

echo "== ELF checks =="
readelf -h "$LIBS/hyperfile-httpd" | grep -E 'Class:|Machine:'
readelf -A "$LIBS/hyperfile-httpd" | grep 'Tag_ABI_VFP_args' || true
readelf -h "$LIBS/libjsapi_hyperfile_http.so" | grep -E 'Class:|Machine:'
readelf -A "$LIBS/libjsapi_hyperfile_http.so" | grep 'Tag_ABI_VFP_args' || true
for f in "$LIBS/hyperfile-httpd" "$LIBS/libjsapi_hyperfile_http.so"; do
  max=$(readelf -V "$f" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 || true)
  echo "$f GLIBC max: ${max:-none}"
  if [ -n "$max" ] && [ "$(printf '%s\n%s\n' "$GLIBC" "${max#GLIBC_}" | sort -V | tail -1)" != "$GLIBC" ]; then
    echo "GLIBC requirement exceeds $GLIBC: $f" >&2
    exit 1
  fi
done
echo "Artifacts copied into libs/."
