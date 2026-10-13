/* PlayKaraoke PS5 — fila com rotação de cantores. Ver queue.h. */

#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void queue_init(queue_t *q) {
  memset(q, 0, sizeof *q);
  q->current = -1;
  q->next_id = 1;
}

void queue_free(queue_t *q) {
  for (int i = 0; i < q->count; i++) free(q->singers[i].songs);
  queue_init(q);
}

static singer_t *find_singer(queue_t *q, const char *name) {
  for (int i = 0; i < q->count; i++)
    if (strcasecmp(q->singers[i].name, name) == 0) return &q->singers[i];
  return NULL;
}

singer_t *queue_singer_for_client(queue_t *q, const char *client) {
  if (!client || !client[0]) return NULL;
  for (int i = 0; i < q->count; i++)
    if (strcmp(q->singers[i].client, client) == 0) return &q->singers[i];
  return NULL;
}

queue_result_t queue_add(queue_t *q, const char *singer_name, const char *path) {
  return queue_add_for(q, singer_name, "", path);
}

queue_result_t queue_add_for(queue_t *q, const char *singer_name, const char *client, const char *path) {
  if (!client) client = "";
  singer_t *s = client[0] ? queue_singer_for_client(q, client) : NULL;
  if (!s) {
    s = find_singer(q, singer_name);
    if (s && strcmp(s->client, client) != 0) return QUEUE_ERR_NAME_TAKEN;
  }
  if (!s) {
    if (q->count == QUEUE_MAX_SINGERS) return QUEUE_ERR_TOO_MANY_SINGERS;
    int host = strcasecmp(singer_name, QUEUE_HOST_NAME) == 0;
    int capacity = host ? QUEUE_HOST_MAX_SONGS : QUEUE_MAX_SONGS;
    qsong_t *songs = calloc((size_t)capacity, sizeof *songs);
    if (!songs) return QUEUE_ERR_MEMORY;
    /* Cantor novo entra no fim da rodada atual = fim da lista. */
    s = &q->singers[q->count++];
    memset(s, 0, sizeof *s);
    s->id = q->next_id++;
    snprintf(s->name, sizeof s->name, "%s", singer_name);
    snprintf(s->client, sizeof s->client, "%s", client);
    s->songs = songs;
    s->capacity = capacity;
    if (q->current < 0) q->current = q->count - 1;
  }
  if (s->count == s->capacity) return QUEUE_ERR_SINGER_FULL;

  qsong_t *song = &s->songs[s->count++];
  memset(song, 0, sizeof *song);
  song->id = q->next_id++;
  snprintf(song->path, sizeof song->path, "%s", path);
  const char *slash = strrchr(path, '/');
  songinfo_parse(slash ? slash + 1 : path, &song->info);
  q->version++;
  return QUEUE_OK;
}

queue_result_t queue_cancel(queue_t *q, int song_id, const char *client) {
  for (int i = 0; i < q->count; i++) {
    singer_t *s = &q->singers[i];
    for (int j = 0; j < s->count; j++) {
      if (s->songs[j].id != song_id) continue;
      if (strcmp(s->client, client ? client : "") != 0) return QUEUE_ERR_NOT_ALLOWED;
      queue_remove_song(q, i, j);
      return QUEUE_OK;
    }
  }
  return QUEUE_ERR_NOT_ALLOWED;
}

int queue_total_songs(const queue_t *q) {
  int total = 0;
  for (int i = 0; i < q->count; i++) total += q->singers[i].count;
  return total;
}

qsong_t *queue_current(queue_t *q, singer_t **singer) {
  if (singer) *singer = NULL;
  if (q->count == 0) return NULL;
  if (q->current < 0 || q->current >= q->count) q->current = 0;
  for (int n = 0; n < q->count; n++) {
    int i = (q->current + n) % q->count;
    if (q->singers[i].count > 0) {
      q->current = i;
      if (singer) *singer = &q->singers[i];
      return &q->singers[i].songs[0];
    }
  }
  return NULL;
}

static void advance(queue_t *q) {
  if (q->count > 0) q->current = (q->current + 1) % q->count;
}

void queue_complete(queue_t *q, int song_id) {
  for (int i = 0; i < q->count; i++) {
    singer_t *s = &q->singers[i];
    for (int j = 0; j < s->count; j++) {
      if (s->songs[j].id != song_id) continue;
      memmove(&s->songs[j], &s->songs[j + 1], (size_t)(s->count - j - 1) * sizeof *s->songs);
      s->count--;
      if (i == q->current) advance(q);
      q->version++;
      return;
    }
  }
}

void queue_skip_turn(queue_t *q) {
  if (queue_current(q, NULL)) advance(q);
  q->version++;
}

int queue_upcoming(queue_t *q, singer_t **singers, qsong_t **songs, int max) {
  if (!queue_current(q, NULL)) return 0;
  int found = 0;
  for (int n = 1; n < q->count && found < max; n++) {
    singer_t *s = &q->singers[(q->current + n) % q->count];
    if (s->count == 0) continue;
    singers[found] = s;
    songs[found] = &s->songs[0];
    found++;
  }
  return found;
}

int queue_singer_index(const queue_t *q, const char *singer_name) {
  for (int i = 0; i < q->count; i++)
    if (strcasecmp(q->singers[i].name, singer_name) == 0) return i;
  return -1;
}

int queue_find(const queue_t *q, const char *singer_name, const char *path) {
  int s = queue_singer_index(q, singer_name);
  if (s < 0) return -1;
  for (int j = 0; j < q->singers[s].count; j++)
    if (strcmp(q->singers[s].songs[j].path, path) == 0) return j;
  return -1;
}

int queue_remove_path(queue_t *q, const char *singer_name, const char *path) {
  int j = queue_find(q, singer_name, path);
  if (j < 0) return -1;
  queue_remove_song(q, queue_singer_index(q, singer_name), j);
  return 0;
}

void queue_remove_song(queue_t *q, int singer_index, int song_index) {
  if (singer_index < 0 || singer_index >= q->count) return;
  singer_t *s = &q->singers[singer_index];
  if (song_index < 0 || song_index >= s->count) return;
  memmove(&s->songs[song_index], &s->songs[song_index + 1],
          (size_t)(s->count - song_index - 1) * sizeof *s->songs);
  s->count--;
  q->version++;
}

int queue_move_song(queue_t *q, int singer_index, int from, int to) {
  if (singer_index < 0 || singer_index >= q->count) return from;
  singer_t *s = &q->singers[singer_index];
  if (from < 0 || from >= s->count) return from;
  if (to < 0) to = 0;
  if (to >= s->count) to = s->count - 1;
  if (to == from) return from;
  qsong_t moved = s->songs[from];
  if (to > from)
    memmove(&s->songs[from], &s->songs[from + 1], (size_t)(to - from) * sizeof *s->songs);
  else
    memmove(&s->songs[to + 1], &s->songs[to], (size_t)(from - to) * sizeof *s->songs);
  s->songs[to] = moved;
  q->version++;
  return to;
}

void queue_remove_singer(queue_t *q, int singer_index) {
  if (singer_index < 0 || singer_index >= q->count) return;
  free(q->singers[singer_index].songs);
  memmove(&q->singers[singer_index], &q->singers[singer_index + 1],
          (size_t)(q->count - singer_index - 1) * sizeof q->singers[0]);
  q->count--;
  /* A vez continua com quem "escorregou" para a posição do removido. */
  if (q->count == 0) q->current = -1;
  else if (singer_index < q->current) q->current--;
  else if (q->current >= q->count) q->current = 0;
  q->version++;
}
