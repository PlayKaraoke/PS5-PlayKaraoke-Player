/* PlayKaraoke PS5 — tela FILA (OPTIONS em qualquer tela, inclusive com
 * música tocando): a rotação a partir de quem está na vez, com as músicas
 * de cada cantor. ↑/↓ escolhem, ✕ pega a música para mudar de lugar (↑/↓
 * movem, ✕ ou ○ soltam), △ remove, ○/OPTIONS fecham. */

#pragma once

#include "queue.h"
#include "ui.h"

typedef struct queue_view {
  int sel, scroll;
  int grabbed; /* música "pega" para mudar de lugar */
  char toast[160];
  Uint32 toast_until;
} queue_view_t;

typedef enum { QV_NONE, QV_UP, QV_DOWN, QV_LEFT, QV_RIGHT, QV_GRAB, QV_REMOVE, QV_CLOSE } qv_action_t;

void queue_view_open(queue_view_t *v);

/* `playing_id`: ID da música tocando agora (0 se nenhuma). */
void queue_view_draw(ui_t *ui, queue_t *q, queue_view_t *v, int playing_id);

/* Devolve 1 quando a tela deve fechar. */
int queue_view_act(queue_t *q, queue_view_t *v, qv_action_t act, int playing_id);

/* Algum aviso na tela que vai sumir sozinho (para redesenhar). */
int queue_view_has_toast(const queue_view_t *v);
