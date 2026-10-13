/* PlayKaraoke PS5 — informações da música a partir do nome do arquivo.
 *
 * Portado do parseKaraokeFilename (js/file-loader.js do web player).
 * Convenção "CÓDIGO - Artista - Música", separada por " - " (com espaços,
 * para não confundir com o hífen dentro do código, ex. "EJBg-0020"):
 *   "EJBg-0020 - Kansas - Play the Game Tonight" → código, artista, título
 *   "SF001-01 - Hello"     → código + título (só prefixo de produtora conhecido)
 *   "Queen - Bohemian Rhapsody" → artista, título
 *   "Arquivo Qualquer"     → só título
 * O primeiro pedaço só vira código se PARECER um código. */

#pragma once

#include <stddef.h>

typedef struct songinfo {
  char code[64];    /* vazio se não houver */
  char artist[256]; /* vazio se não houver */
  char title[256];
} songinfo_t;

/* `filename` sem pasta (com ou sem .zip/.mp4). */
void songinfo_parse(const char *filename, songinfo_t *out);

/* "Artista – Título" (ou só o título). */
void songinfo_display(const songinfo_t *info, char *out, size_t size);
