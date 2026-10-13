#!/usr/bin/env bash
# Testes da lógica pura do app (sem PS5): rodam no Mac/Linux com clang.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
APP="$ROOT/src/app"

run() {
  local name=$1; shift
  echo "== $name"
  clang -Wall -Werror -I "$APP" -o "$OUT/$name" "$ROOT/tests/$name.c" "$@"
  "$OUT/$name"
}

run test_songinfo "$APP/songinfo.c"
run test_queue "$APP/queue.c" "$APP/songinfo.c"
run test_library "$APP/library.c" "$APP/songinfo.c"
