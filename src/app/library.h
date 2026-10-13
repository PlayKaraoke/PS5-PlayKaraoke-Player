/* PlayKaraoke PS5 — biblioteca: índice das músicas dos HDs e busca.
 *
 * Mesmo princípio do js/library-worker.js do web player: o índice é só
 * texto (caminho do arquivo + texto de busca sem acento), fica salvo entre
 * sessões e a busca exige que todas as palavras apareçam no título, artista
 * ou código. A varredura roda numa thread separada; enquanto isso, o índice
 * anterior continua valendo. Usa pthreads (sem SDL), então roda nos testes. */

#pragma once

#include <pthread.h>

#define LIBRARY_MAX_RESULTS 100

typedef struct library {
  pthread_mutex_t lock;
  char **paths;  /* caminho completo de cada música */
  char **texts;  /* texto de busca (minúsculas, sem acento) */
  int count;

  /* varredura em segundo plano */
  pthread_t thread;
  int thread_started;
  volatile int scanning;
  volatile int scan_found; /* músicas encontradas até agora */
  volatile int cancel;     /* pede para a varredura parar (ao sair) */
  char **roots;
  int root_count;
  char index_file[512];
} library_t;

void library_init(library_t *lib);
void library_free(library_t *lib);

/* Carrega o índice salvo. Devolve o número de músicas (0 se não houver). */
int library_load(library_t *lib, const char *index_file);

/* Varre as pastas em segundo plano; ao terminar troca o índice e salva em
 * `index_file`. Ignora o pedido se já houver uma varredura em andamento. */
int library_scan_async(library_t *lib, const char *const *roots, int root_count, const char *index_file);

int library_count(library_t *lib);

typedef enum { LIBRARY_ALL, LIBRARY_ONLY_CDG, LIBRARY_ONLY_MP4 } library_filter_t;

/* Busca em ordem alfabética (artista, depois título, sem acento) e copia
 * até `max` caminhos para `out`. Devolve quantos copiou; em *total, quantas
 * músicas casaram (pode ser mais que `max`). */
int library_search(library_t *lib, const char *query, library_filter_t filter,
                   char (*out)[1024], int max, int *total);

/* Minúsculas e sem acento (UTF-8 Latin-1 → ASCII), para buscar. */
void library_fold(const char *in, char *out, size_t size);
