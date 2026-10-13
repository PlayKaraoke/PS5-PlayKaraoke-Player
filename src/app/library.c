/* PlayKaraoke PS5 — biblioteca: índice e busca. Ver library.h. */

#include "library.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "songinfo.h"

#define INDEX_MAGIC "PKIDX1"
#define MAX_WORDS 8

/* ---------- Texto de busca ---------- */

/* U+00C0..U+00FF → letra sem acento (UTF-8: 0xC3 0x80..0xBF). */
static const char LATIN1_FOLD[] = "aaaaaaaceeeeiiiidnoooooxouuuuytsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

void library_fold(const char *in, char *out, size_t size) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < size; p++) {
    if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
      out[o++] = LATIN1_FOLD[p[1] - 0x80];
      p++;
    } else {
      out[o++] = (char)tolower(*p);
    }
  }
  out[o] = '\0';
}

static char *search_text_for(const char *path) {
  const char *slash = strrchr(path, '/');
  songinfo_t info;
  songinfo_parse(slash ? slash + 1 : path, &info);
  char joined[700], folded[700];
  snprintf(joined, sizeof joined, "%s %s %s", info.title, info.artist, info.code);
  library_fold(joined, folded, sizeof folded);
  return strdup(folded);
}

/* ---------- Lista de músicas ---------- */

typedef struct list {
  char **paths, **texts;
  int count, cap;
} list_t;

static int list_add(list_t *l, const char *path) {
  if (l->count == l->cap) {
    int cap = l->cap ? l->cap * 2 : 1024;
    char **p = realloc(l->paths, (size_t)cap * sizeof *p);
    if (!p) return -1;
    l->paths = p;
    char **t = realloc(l->texts, (size_t)cap * sizeof *t);
    if (!t) return -1;
    l->texts = t;
    l->cap = cap;
  }
  char *path_copy = strdup(path);
  char *text = search_text_for(path);
  if (!path_copy || !text) {
    free(path_copy);
    free(text);
    return -1;
  }
  l->paths[l->count] = path_copy;
  l->texts[l->count] = text;
  l->count++;
  return 0;
}

static void list_free(list_t *l) {
  for (int i = 0; i < l->count; i++) {
    free(l->paths[i]);
    free(l->texts[i]);
  }
  free(l->paths);
  free(l->texts);
  memset(l, 0, sizeof *l);
}

/* Troca o índice atual pela lista nova (a antiga é liberada). */
static void library_swap(library_t *lib, list_t *l) {
  pthread_mutex_lock(&lib->lock);
  list_t old = {lib->paths, lib->texts, lib->count, lib->count};
  lib->paths = l->paths;
  lib->texts = l->texts;
  lib->count = l->count;
  pthread_mutex_unlock(&lib->lock);
  list_free(&old);
  memset(l, 0, sizeof *l);
}

void library_init(library_t *lib) {
  memset(lib, 0, sizeof *lib);
  pthread_mutex_init(&lib->lock, NULL);
}

void library_free(library_t *lib) {
  lib->cancel = 1;
  if (lib->thread_started) pthread_join(lib->thread, NULL);
  list_t l = {lib->paths, lib->texts, lib->count, lib->count};
  list_free(&l);
  for (int i = 0; i < lib->root_count; i++) free(lib->roots[i]);
  free(lib->roots);
  pthread_mutex_destroy(&lib->lock);
  memset(lib, 0, sizeof *lib);
}

int library_count(library_t *lib) {
  pthread_mutex_lock(&lib->lock);
  int n = lib->count;
  pthread_mutex_unlock(&lib->lock);
  return n;
}

/* ---------- Arquivo do índice ---------- */

int library_load(library_t *lib, const char *index_file) {
  FILE *f = fopen(index_file, "r");
  if (!f) return 0;
  char line[1100];
  list_t l = {0};
  if (!fgets(line, sizeof line, f) || strncmp(line, INDEX_MAGIC, strlen(INDEX_MAGIC)) != 0) {
    fclose(f);
    return 0;
  }
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\n")] = '\0';
    if (line[0] && list_add(&l, line) != 0) break;
  }
  fclose(f);
  int n = l.count;
  library_swap(lib, &l);
  return n;
}

static int save_index(const list_t *l, const char *index_file) {
  char tmp[600];
  snprintf(tmp, sizeof tmp, "%s.tmp", index_file);
  FILE *f = fopen(tmp, "w");
  if (!f) return -1;
  int ok = fprintf(f, "%s\n", INDEX_MAGIC) > 0;
  for (int i = 0; ok && i < l->count; i++) ok = fprintf(f, "%s\n", l->paths[i]) > 0;
  if (fclose(f) != 0) ok = 0;
  if (!ok || rename(tmp, index_file) != 0) {
    remove(tmp);
    return -1;
  }
  return 0;
}

/* ---------- Varredura ---------- */

static int is_song_name(const char *name) {
  size_t len = strlen(name);
  return len > 4 && (strcasecmp(name + len - 4, ".zip") == 0 || strcasecmp(name + len - 4, ".mp4") == 0);
}

static void walk(library_t *lib, list_t *l, const char *dir, int depth) {
  if (depth > 16 || lib->cancel) return;
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *e;
  char path[1024];
  while (!lib->cancel && (e = readdir(d))) {
    const char *name = e->d_name;
    /* Ocultos, fantasmas "._x" do macOS e pastas de sistema. */
    if (name[0] == '.' || strcmp(name, "__MACOSX") == 0 || strncmp(name, "$RECYCLE", 8) == 0 ||
        strcmp(name, "System Volume Information") == 0)
      continue;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int is_dir = e->d_type == DT_DIR;
    if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) {
      struct stat st;
      if (stat(path, &st) != 0) continue;
      is_dir = S_ISDIR(st.st_mode);
    }
    if (is_dir) {
      walk(lib, l, path, depth + 1);
    } else if (is_song_name(name) && list_add(l, path) == 0) {
      lib->scan_found = l->count;
    }
  }
  closedir(d);
}

static void *scan_thread(void *arg) {
  library_t *lib = arg;
  list_t l = {0};
  for (int i = 0; i < lib->root_count; i++) walk(lib, &l, lib->roots[i], 0);
  if (lib->cancel) {
    list_free(&l);
    lib->scanning = 0;
    return NULL;
  }
  printf("[library] varredura: %d musicas\n", l.count);
  if (save_index(&l, lib->index_file) != 0) printf("[library] nao salvou %s\n", lib->index_file);
  library_swap(lib, &l);
  lib->scanning = 0;
  return NULL;
}

int library_scan_async(library_t *lib, const char *const *roots, int root_count, const char *index_file) {
  if (lib->scanning) return -1;
  if (lib->thread_started) {
    pthread_join(lib->thread, NULL);
    lib->thread_started = 0;
  }
  for (int i = 0; i < lib->root_count; i++) free(lib->roots[i]);
  free(lib->roots);
  lib->roots = calloc((size_t)(root_count ? root_count : 1), sizeof *lib->roots);
  if (!lib->roots) return -1;
  lib->root_count = 0;
  for (int i = 0; i < root_count; i++)
    if ((lib->roots[lib->root_count] = strdup(roots[i]))) lib->root_count++;
  snprintf(lib->index_file, sizeof lib->index_file, "%s", index_file);

  lib->scan_found = 0;
  lib->scanning = 1;
  if (pthread_create(&lib->thread, NULL, scan_thread, lib) != 0) {
    lib->scanning = 0;
    return -1;
  }
  lib->thread_started = 1;
  return 0;
}

/* ---------- Busca ---------- */

static int is_mp4(const char *path) {
  size_t len = strlen(path);
  return len > 4 && strcasecmp(path + len - 4, ".mp4") == 0;
}

typedef struct match {
  char key[520]; /* "artista título" sem acento, para ordenar */
  int index;
} match_t;

static int compare_matches(const void *a, const void *b) {
  return strcmp(((const match_t *)a)->key, ((const match_t *)b)->key);
}

int library_search(library_t *lib, const char *query, library_filter_t filter,
                   char (*out)[1024], int max, int *total) {
  if (total) *total = 0;
  char folded[512];
  library_fold(query, folded, sizeof folded);
  char *words[MAX_WORDS];
  int nwords = 0;
  for (char *tok = strtok(folded, " \t"); tok && nwords < MAX_WORDS; tok = strtok(NULL, " \t"))
    words[nwords++] = tok;
  if (nwords == 0) return 0;

  /* Junta todas as que casam, ordena e devolve as primeiras `max`. */
  match_t *matches = NULL;
  int found = 0, cap = 0;
  pthread_mutex_lock(&lib->lock);
  for (int i = 0; i < lib->count; i++) {
    const char *path = lib->paths[i];
    if ((filter == LIBRARY_ONLY_CDG && is_mp4(path)) || (filter == LIBRARY_ONLY_MP4 && !is_mp4(path)))
      continue;
    int ok = 1;
    for (int w = 0; w < nwords && ok; w++) ok = strstr(lib->texts[i], words[w]) != NULL;
    if (!ok) continue;
    if (found == cap) {
      int n = cap ? cap * 2 : 256;
      match_t *m = realloc(matches, (size_t)n * sizeof *m);
      if (!m) break;
      matches = m;
      cap = n;
    }
    const char *slash = strrchr(path, '/');
    songinfo_t info;
    songinfo_parse(slash ? slash + 1 : path, &info);
    char key[520];
    snprintf(key, sizeof key, "%s %s", info.artist[0] ? info.artist : info.title, info.title);
    library_fold(key, matches[found].key, sizeof matches[found].key);
    matches[found].index = i;
    found++;
  }
  qsort(matches, (size_t)found, sizeof *matches, compare_matches);
  int copied = found < max ? found : max;
  for (int i = 0; i < copied; i++) snprintf(out[i], 1024, "%s", lib->paths[matches[i].index]);
  pthread_mutex_unlock(&lib->lock);
  free(matches);
  if (total) *total = found;
  return copied;
}
