/* PlayKaraoke PS5 — fila com rotação de cantores.
 *
 * Portado do js/singers.js do web player, com as regras da seção 5 do
 * PLANO.md:
 *  - cantores entram na rotação na ordem em que pedem a primeira música;
 *    cantor novo entra no fim da rodada atual;
 *  - cada cantor canta 1 música por rodada (a próxima da lista dele);
 *  - até 5 músicas por cantor (o cantor "PS5", do dono, não tem limite);
 *  - cantor sem música quando chega a vez é pulado, mas continua na rotação;
 *  - cada música guarda o próprio tom.
 * Músicas são identificadas por ID (não pela posição), então mexer na fila
 * durante a apresentação não consome a música errada. Sem dependências. */

#pragma once

#include "songinfo.h"

#define QUEUE_MAX_SINGERS 64
#define QUEUE_MAX_SONGS 5        /* por cantor */
#define QUEUE_HOST_MAX_SONGS 200 /* cantor "PS5" */
#define QUEUE_HOST_NAME "PS5"

typedef struct qsong {
  int id;
  char path[1024];
  songinfo_t info;
  int semitones;
} qsong_t;

typedef struct singer {
  int id;
  char name[64];
  char client[48]; /* ID do celular do cantor ("" = o dono, pelo DualSense) */
  qsong_t *songs; /* alocado com a capacidade do cantor */
  int count, capacity;
} singer_t;

typedef struct queue {
  singer_t singers[QUEUE_MAX_SINGERS];
  int count;
  int current; /* índice do cantor da vez (-1 = ninguém) */
  int next_id;
  unsigned version; /* muda a cada alteração (para o servidor) */
} queue_t;

typedef enum {
  QUEUE_OK = 0,
  QUEUE_ERR_SINGER_FULL = -1, /* cantor já tem o máximo de músicas */
  QUEUE_ERR_TOO_MANY_SINGERS = -2,
  QUEUE_ERR_MEMORY = -3,
  QUEUE_ERR_NAME_TAKEN = -4,  /* outro celular já usa esse nome */
  QUEUE_ERR_NOT_ALLOWED = -5, /* a música é de outro cantor */
} queue_result_t;

void queue_init(queue_t *q);
void queue_free(queue_t *q);

/* Adiciona a música à lista do cantor (cria o cantor no fim da rotação se
 * ainda não existir). O nome é comparado sem diferenciar maiúsculas. */
queue_result_t queue_add(queue_t *q, const char *singer_name, const char *path);

/* Igual, para um pedido do celular `client`: o cantor é achado pelo ID do
 * celular; um nome já usado por outro celular é recusado. */
queue_result_t queue_add_for(queue_t *q, const char *singer_name, const char *client, const char *path);

/* Igual, com as informações da música já prontas (ex.: vídeo do YouTube,
 * que não tem nome de arquivo para ler). `info` NULL = lê do nome. */
queue_result_t queue_add_full(queue_t *q, const char *singer_name, const char *client, const char *path,
                              const songinfo_t *info);

/* Cantor do celular `client`, ou NULL. */
singer_t *queue_singer_for_client(queue_t *q, const char *client);

/* O celular `client` cancela uma música dele. */
queue_result_t queue_cancel(queue_t *q, int song_id, const char *client);

int queue_total_songs(const queue_t *q);

/* Música da vez: a partir do cantor atual, pula quem estiver sem música.
 * Atualiza `current`. NULL se não houver nenhuma música. */
qsong_t *queue_current(queue_t *q, singer_t **singer);

/* A música `song_id` foi cantada: sai da lista e a vez passa ao próximo. */
void queue_complete(queue_t *q, int song_id);

/* Pula a vez do cantor atual sem consumir a música. */
void queue_skip_turn(queue_t *q);

/* Próximos cantores com música depois do atual (para "A seguir"). Preenche
 * até `max` pares cantor/música; devolve quantos. */
int queue_upcoming(queue_t *q, singer_t **singers, qsong_t **songs, int max);

/* A música `path` está na lista de `singer_name`? Devolve o índice dela
 * na lista do cantor, ou -1. */
int queue_find(const queue_t *q, const char *singer_name, const char *path);

/* Tira a música `path` da lista de `singer_name`. Devolve 0 se tirou. */
int queue_remove_path(queue_t *q, const char *singer_name, const char *path);

/* Índice do cantor pelo nome, ou -1. */
int queue_singer_index(const queue_t *q, const char *singer_name);

void queue_remove_song(queue_t *q, int singer_index, int song_index);

/* Muda a posição de uma música na lista do cantor. Devolve a nova posição
 * (ou `from`, se não deu para mover). */
int queue_move_song(queue_t *q, int singer_index, int from, int to);
void queue_remove_singer(queue_t *q, int singer_index);
