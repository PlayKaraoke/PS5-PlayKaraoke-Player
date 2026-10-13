/* PlayKaraoke PS5 — busca online (YouTube) pelo mesmo Worker do web player.
 *
 * O Worker (Cloudflare) guarda a chave da YouTube Data API e só aceita a
 * origem do site do web player: o PS5 manda esse mesmo Origin. Na fila, uma
 * música do YouTube é o caminho "yt:<videoId>"; na vez dela o app abre o
 * vídeo no navegador do PS5, no player oficial (sem baixar nada). */

#pragma once

#include <stddef.h>

#include "songinfo.h"

#define ONLINE_MAX_RESULTS 20
#define ONLINE_PREFIX "yt:"

typedef struct online_result {
  char video_id[32];
  char title[256];   /* título do vídeo como veio do YouTube */
  char channel[128];
} online_result_t;

/* Chamar uma vez no início do app. */
void online_init(void);

/* Busca (bloqueia ~1 s; pode ser chamada de qualquer thread). Devolve o
 * número de resultados, ou -1 com a mensagem em `error`. */
int online_search(const char *query, online_result_t *out, int max, char *error, size_t error_size);

/* "yt:<id>" é um ID de vídeo válido? */
int online_is_path(const char *path);

/* Artista/título a partir do título do vídeo (como o buildOnlineItem do web
 * player: tira "karaoke version" e separa por " - ", " | "...). */
void online_song_info(const char *video_title, songinfo_t *out);

/* Endereço para abrir o vídeo no navegador. */
void online_watch_url(const char *path, char *out, size_t size);
