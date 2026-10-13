/* PlayKaraoke PS5 — abre um ZIP de karaokê (MP3+G).
 *
 * Mesmas regras do js/file-loader.js do web player: ignora o lixo do
 * macOS/Windows (__MACOSX/, "._nome", .DS_Store, Thumbs.db), pega o primeiro
 * .cdg e, havendo mais de um áudio, prefere o de mesmo nome do .cdg. */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Lê o .cdg para a memória (*cdg, liberar com free) e grava o áudio em
 * `audio_path` + extensão original (o caminho final vai em `audio_out`).
 * Devolve 0 em caso de sucesso; erros são logados. */
int kzip_extract(const char *zip_path, uint8_t **cdg, size_t *cdg_size,
                 const char *audio_path, char *audio_out, size_t audio_out_size);
