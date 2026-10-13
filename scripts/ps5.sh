#!/usr/bin/env bash
# PlayKaraoke PS5 — compila o payload no container e roda no PS5.
#
# Uso (no Mac, de qualquer pasta):
#   scripts/ps5.sh            compila e injeta o launcher (padrão); depois
#                             é só abrir o PlayKaraoke em Mídia
#   scripts/ps5.sh build      só compila
#   scripts/ps5.sh send       injeta o último build/playkaraoke.elf
#   scripts/ps5.sh media      envia assets/test/* para /data/playkaraoke/test (FTP)
#   scripts/ps5.sh logs       mostra launcher.log e app.log do PS5 (FTP)
#   scripts/ps5.sh release    versão pública (sem ferramentas de desenvolvimento)
#                             em dist/playkaraoke.elf
#   scripts/ps5.sh clean      apaga build/
#   scripts/ps5.sh shell      abre um shell no container (depuração)
#   scripts/ps5.sh image      (re)constrói a imagem Docker
#
# Só um processo "BigApp" tem acesso à tela. O launcher (src/launcher) cria
# esse processo quando o ícone é aberto.
#
# Variáveis opcionais: PS5_HOST (padrão 192.168.31.35), PS5_PORT (elfldr,
# padrão 9021), FTP_PORT (padrão 2121).
# A saída (printf) do app aparece aqui até ele terminar; Ctrl+C só
# desconecta o terminal, não fecha o app no PS5.

set -euo pipefail

PS5_HOST="${PS5_HOST:-192.168.31.35}"
PS5_PORT="${PS5_PORT:-9021}"
IMAGE="playkaraoke-ps5-sdk:pacbrew-v0.41.1"
ELF="build/playkaraoke.elf"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# O Docker Desktop nem sempre coloca o docker no PATH.
if ! command -v docker >/dev/null 2>&1 && [ -x /Applications/Docker.app/Contents/Resources/bin/docker ]; then
  PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
fi
if ! command -v docker >/dev/null 2>&1; then
  echo "erro: docker não encontrado. Instale e abra o Docker Desktop." >&2
  exit 1
fi

TTY_FLAGS=()
[ -t 0 ] && [ -t 1 ] && TTY_FLAGS=(-it)

in_container() {
  docker run --rm ${TTY_FLAGS[@]+"${TTY_FLAGS[@]}"} -v "$ROOT":/work -w /work "$IMAGE" "$@"
}

ensure_image() {
  if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    build_image
  fi
}

build_image() {
  echo "==> construindo a imagem $IMAGE (primeira vez baixa ~230 MB)"
  docker build -t "$IMAGE" "$ROOT/docker"
}

do_build() {
  ensure_image
  echo "==> compilando $ELF"
  in_container make
}

do_send() {
  if [ ! -f "$ROOT/$ELF" ]; then
    echo "erro: $ELF não existe. Rode: scripts/ps5.sh build" >&2
    exit 1
  fi
  ensure_image
  echo "==> enviando $ELF para $PS5_HOST:$PS5_PORT"
  in_container prospero-deploy -h "$PS5_HOST" -p "$PS5_PORT" "$ELF"
}

FTP_PORT="${FTP_PORT:-2121}"

# Envia assets/test/* para /data/playkaraoke/test no PS5 (precisa do ftpsrv).
do_media() {
  local f
  for f in "$ROOT"/assets/test/*; do
    [ -f "$f" ] || continue
    echo "==> enviando $(basename "$f")"
    curl -fsS --ftp-create-dirs -T "$f" \
      "ftp://$PS5_HOST:$FTP_PORT/data/playkaraoke/test/$(basename "$f" | sed 's/ /%20/g')" \
      || { echo "erro: FTP falhou (o ftpsrv está ativo na porta $FTP_PORT?)" >&2; exit 1; }
  done
}

# Mostra os logs do app e do launcher (precisa do ftpsrv).
do_logs() {
  local f
  for f in launcher.log app.log; do
    echo "===== $f"
    curl -fsS "ftp://$PS5_HOST:$FTP_PORT/data/playkaraoke/logs/$f" || echo "(não consegui ler $f)"
  done
}

case "${1:-run}" in
  run)     do_build; do_send ;;
  build)   do_build ;;
  send)    do_send ;;
  release)
    ensure_image
    echo "==> versão pública (DEV=0)"
    mkdir -p "$ROOT/dist"
    in_container bash -c 'make clean && make DEV=0 && cp build/playkaraoke.elf dist/playkaraoke.elf && make clean'
    ls -la "$ROOT/dist/playkaraoke.elf"
    ;;
  media)   do_media ;;
  logs)    do_logs ;;
  clean) ensure_image; in_container make clean ;;
  shell) ensure_image; in_container bash ;;
  image) build_image ;;
  *) echo "comando desconhecido: $1 (use run, build, send, release, media, logs, clean, shell ou image)" >&2; exit 1 ;;
esac
