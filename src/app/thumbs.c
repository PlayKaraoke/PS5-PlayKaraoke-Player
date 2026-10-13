/* PlayKaraoke PS5 — miniaturas dos vídeos do YouTube. Ver thumbs.h. */

#include "thumbs.h"

#include <stdlib.h>
#include <string.h>

#include <SDL_image.h>

#define THUMB_MAX_BYTES (512 * 1024)

typedef struct job {
  thumbs_t *t;
  unsigned generation;
  int count;
  char urls[ONLINE_MAX_RESULTS][256];
} job_t;

void thumbs_init(thumbs_t *t) {
  memset(t, 0, sizeof *t);
  pthread_mutex_init(&t->lock, NULL);
}

/* JPEG baixado → superfície THUMB_W x THUMB_H (ARGB). */
static SDL_Surface *decode(const unsigned char *data, size_t size) {
  SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
  SDL_Surface *src = rw ? IMG_Load_RW(rw, 1) : NULL;
  if (!src) return NULL;
  SDL_Surface *argb = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(src);
  if (!argb) return NULL;
  SDL_Surface *small = SDL_CreateRGBSurfaceWithFormat(0, THUMB_W, THUMB_H, 32, SDL_PIXELFORMAT_ARGB8888);
  if (small) SDL_BlitScaled(argb, NULL, small, NULL);
  SDL_FreeSurface(argb);
  return small;
}

static void *run(void *arg) {
  job_t *job = arg;
  thumbs_t *t = job->t;
  for (int i = 0; i < job->count; i++) {
    if (t->generation != job->generation) break; /* busca nova: desiste */
    unsigned char *data;
    size_t size;
    if (!job->urls[i][0] || online_fetch(job->urls[i], &data, &size, THUMB_MAX_BYTES) != 0) continue;
    SDL_Surface *img = decode(data, size);
    free(data);
    if (!img) continue;
    pthread_mutex_lock(&t->lock);
    if (t->generation == job->generation && !t->img[i]) {
      t->img[i] = img;
      t->changed = 1;
      img = NULL;
    }
    pthread_mutex_unlock(&t->lock);
    if (img) SDL_FreeSurface(img);
  }
  free(job);
  return NULL;
}

void thumbs_load(thumbs_t *t, const online_result_t *results, int count) {
  job_t *job = calloc(1, sizeof *job);
  pthread_mutex_lock(&t->lock);
  t->generation++;
  for (int i = 0; i < ONLINE_MAX_RESULTS; i++) {
    if (t->img[i]) SDL_FreeSurface(t->img[i]);
    t->img[i] = NULL;
  }
  t->changed = 1;
  unsigned generation = t->generation;
  pthread_mutex_unlock(&t->lock);
  if (!job) return;

  job->t = t;
  job->generation = generation;
  job->count = count > ONLINE_MAX_RESULTS ? ONLINE_MAX_RESULTS : count;
  for (int i = 0; i < job->count; i++) strncpy(job->urls[i], results[i].thumb, sizeof job->urls[i] - 1);
  pthread_t thread;
  if (pthread_create(&thread, NULL, run, job) != 0) {
    free(job);
    return;
  }
  pthread_detach(thread);
}

SDL_Surface *thumbs_get(thumbs_t *t, int i) {
  if (i < 0 || i >= ONLINE_MAX_RESULTS) return NULL;
  pthread_mutex_lock(&t->lock);
  SDL_Surface *img = t->img[i];
  pthread_mutex_unlock(&t->lock);
  return img;
}

int thumbs_take_changed(thumbs_t *t) {
  pthread_mutex_lock(&t->lock);
  int changed = t->changed;
  t->changed = 0;
  pthread_mutex_unlock(&t->lock);
  return changed;
}
