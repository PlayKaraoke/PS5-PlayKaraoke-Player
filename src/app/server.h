/* PlayKaraoke PS5 — servidor web para os celulares (libmicrohttpd).
 *
 * Roda na thread do libmicrohttpd e NÃO mexe na fila: os pedidos que mudam
 * algo viram comandos que o laço principal do app executa (server_take /
 * server_done), e o estado é um "retrato" em JSON que o app publica a cada
 * mudança (server_set_state). A busca vai direto na biblioteca, que é
 * protegida por mutex.
 *
 * Rotas (parâmetros na URL):
 *   GET  /                    página do celular
 *   GET  /api/state           estado da fila (o celular consulta a cada 1 s)
 *   GET  /api/search?q=&f=    busca (f: 0 todos, 1 só CDG, 2 só MP4)
 *   POST /api/queue?client=&name=&path=      pede uma música
 *   POST /api/cancel?client=&id=             cancela uma música sua
 *   POST /api/pitch?client=&id=&semitones=   tom de uma música sua
 *   POST /api/finish?client=                 termina a sua música
 * Só na versão de desenvolvimento (PK_DEV_TOOLS), para tirar prints:
 *   POST /api/dev/shot                       salva a tela em SCREENSHOT_FILE
 *   POST /api/dev/input?k=cross|circle|...&text=   simula o controle/teclado */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "library.h"

typedef enum { CMD_ADD, CMD_CANCEL, CMD_PITCH, CMD_FINISH, CMD_SHOT } server_cmd_type_t;

typedef struct server_cmd {
  server_cmd_type_t type;
  char client[48];
  char name[64];
  char path[1024];
  int song_id;
  int semitones;

  /* resposta, preenchida pelo app */
  int status;       /* HTTP: 200, 400, 403, 409... */
  char body[512];   /* JSON */

  int done, abandoned;
  struct server_cmd *next;
} server_cmd_t;

typedef struct server server_t;

/* `wake` é chamado (na thread do servidor) quando chega um comando, para o
 * app acordar o laço principal. */
server_t *server_start(int port, library_t *lib, const uint8_t *page, size_t page_size,
                       void (*wake)(void *ctx), void *wake_ctx);
void server_stop(server_t *s);

void server_set_state(server_t *s, const char *json);

/* Tira os comandos pendentes (lista ligada, na ordem de chegada). */
server_cmd_t *server_take(server_t *s);
int server_pending(server_t *s);

/* O app terminou o comando (com status e body preenchidos). */
void server_done(server_t *s, server_cmd_t *cmd);

/* JSON: acrescenta `text` entre aspas, com escape. */
void json_append_string(char *out, size_t size, const char *text);

/* Hash curto (hex) do ID do celular, para o estado público. */
void server_client_tag(const char *client, char out[9]);
