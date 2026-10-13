/* PlayKaraoke PS5 — tela TOCANDO: toca uma música (ZIP com CDG + áudio ou
 * MP4) em tela cheia até ela acabar ou o dono pular.
 *
 * Controles: ✕ pausa/retoma, ○ encerra a música, ↑/↓ ou analógico esquerdo
 * volume, analógico direito ←/→ volta/avança 10 s, L1/R1 tom −1/+1,
 * OPTIONS abre uma tela por cima (a fila) com a música continuando, touchpad
 * mostra uma barra com os comandos. */

#pragma once

#include <stddef.h>

#include <SDL.h>

#include "osd.h"

/* Pedidos de fora (celular) para a música que está tocando. */
typedef struct playback_remote {
  int set_pitch; /* 1 = mudar o tom para `semitones` */
  int semitones;
  int finish;    /* 1 = terminar a música */
} playback_remote_t;

/* Ganchos do app durante a música: a tela que abre por cima com OPTIONS (a
 * fila) e o `tick`, chamado a cada volta do laço (pedidos dos celulares). */
typedef struct playback_overlay {
  void *ctx;
  void (*open)(void *ctx);
  void (*draw)(void *ctx);                     /* desenha a tela inteira */
  int (*event)(void *ctx, const SDL_Event *e); /* devolve 1 para fechar */
  int (*animating)(void *ctx);                 /* algo some sozinho: redesenhar */
  void (*tick)(void *ctx, playback_remote_t *remote);
  void (*decorate)(void *ctx); /* desenha por cima da música (ex.: QR code) */
} playback_overlay_t;

typedef enum {
  PLAYBACK_ENDED,   /* chegou ao fim */
  PLAYBACK_STOPPED, /* o dono encerrou com ○ */
  PLAYBACK_FAILED,  /* não abriu (motivo em `error`) */
} playback_result_t;

/* `semitones` (tom da música) e `volume` (0-100, da sessão) entram e saem
 * atualizados. `display` é o nome da música ("Artista – Título"), mostrado
 * na tela de espera das músicas do YouTube ("yt:<id>"), que tocam no
 * navegador do PS5. */
playback_result_t playback_play(SDL_Window *win, osd_t *osd, const playback_overlay_t *overlay,
                                const char *path, const char *display, int *semitones, int *volume,
                                char *error, size_t error_size);
