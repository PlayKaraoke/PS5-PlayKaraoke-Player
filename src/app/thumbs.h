/* PlayKaraoke PS5 — miniaturas dos vídeos do YouTube na busca da TV.
 *
 * Baixadas em segundo plano (thread) depois de cada busca e reduzidas para
 * THUMB_W x THUMB_H; a lista aparece na hora e as imagens vão surgindo. Uma
 * busca nova descarta as imagens que ainda chegariam da anterior. */

#pragma once

#include <pthread.h>

#include <SDL.h>

#include "online.h"

#define THUMB_W 96
#define THUMB_H 54

typedef struct thumbs {
  pthread_mutex_t lock;
  SDL_Surface *img[ONLINE_MAX_RESULTS];
  unsigned generation; /* muda a cada busca */
  int changed;         /* chegou imagem nova (para redesenhar) */
} thumbs_t;

void thumbs_init(thumbs_t *t);

/* Descarta as miniaturas atuais e começa a baixar as destes resultados. */
void thumbs_load(thumbs_t *t, const online_result_t *results, int count);

/* Miniatura do resultado i (NULL se ainda não chegou). Só usar na thread
 * principal (é ela que libera as antigas). */
SDL_Surface *thumbs_get(thumbs_t *t, int i);

/* 1 se chegou imagem nova desde a última chamada. */
int thumbs_take_changed(thumbs_t *t);
