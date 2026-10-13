/* PlayKaraoke PS5 — navegador de arquivos. Ver browser.h. */

#include "browser.h"
#include "i18n.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define TEST_DIR "/data/playkaraoke/test"
#define MIN_DISK_GB 1.0

void browser_init(browser_t *b) {
  memset(b, 0, sizeof *b);
}

void browser_free(browser_t *b) {
  free(b->entries);
  browser_init(b);
}

static bentry_t *add_entry(browser_t *b) {
  if (b->count == b->capacity) {
    int cap = b->capacity ? b->capacity * 2 : 64;
    bentry_t *e = realloc(b->entries, (size_t)cap * sizeof *e);
    if (!e) return NULL;
    b->entries = e;
    b->capacity = cap;
  }
  bentry_t *e = &b->entries[b->count++];
  memset(e, 0, sizeof *e);
  return e;
}

static int is_song_name(const char *name) {
  size_t len = strlen(name);
  return len > 4 && (strcasecmp(name + len - 4, ".zip") == 0 || strcasecmp(name + len - 4, ".mp4") == 0);
}

static int has_entries(const char *path) {
  DIR *d = opendir(path);
  if (!d) return 0;
  struct dirent *e;
  int found = 0;
  while (!found && (e = readdir(d))) found = e->d_name[0] != '.';
  closedir(d);
  return found;
}

void browser_open_root(browser_t *b) {
  b->count = 0;
  b->sel = b->scroll = 0;
  b->path[0] = '\0';
  b->root_path[0] = '\0';

  char path[64];
  for (int i = 0; i < 8; i++) {
    snprintf(path, sizeof path, "/mnt/usb%d", i);
    /* HDs formatados no Mac/Windows têm uma partição EFI escondida (~200 MB)
     * antes da principal; o PS5 monta as duas (usb0 e usb1). Esconde as
     * partições pequenas para não parecer um segundo HD. */
    struct statfs fs;
    if (statfs(path, &fs) != 0) continue;
    double gb = (double)fs.f_blocks * fs.f_bsize / 1e9;
    if (gb < MIN_DISK_GB || !has_entries(path)) continue;
    bentry_t *e = add_entry(b);
    if (!e) return;
    if (gb >= 1000) snprintf(e->label, sizeof e->label, TR("External drive (%.1f TB)", "HD externo (%.1f TB)"), gb / 1000);
    else snprintf(e->label, sizeof e->label, TR("External drive (%.0f GB)", "HD externo (%.0f GB)"), gb);
    snprintf(e->path, sizeof e->path, "%s", path);
    e->is_dir = 1;
  }
  if (has_entries("/mnt/ext0")) {
    bentry_t *e = add_entry(b);
    if (!e) return;
    snprintf(e->label, sizeof e->label, TR("Extended storage", "Armazenamento estendido"));
    snprintf(e->path, sizeof e->path, "/mnt/ext0");
    e->is_dir = 1;
  }
  bentry_t *e = add_entry(b);
  if (!e) return;
  snprintf(e->label, sizeof e->label, TR("PlayKaraoke test folder", "Pasta de teste do PlayKaraoke"));
  snprintf(e->path, sizeof e->path, "%s", TEST_DIR);
  e->is_dir = 1;
}

static int compare_entries(const void *a, const void *b) {
  const bentry_t *x = a, *y = b;
  if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
  return strcasecmp(x->label, y->label);
}

int browser_open(browser_t *b, const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    printf("[browser] nao abriu %s\n", path);
    return -1;
  }
  if (!b->root_path[0]) snprintf(b->root_path, sizeof b->root_path, "%s", path);
  snprintf(b->path, sizeof b->path, "%s", path);
  b->count = 0;
  b->sel = b->scroll = 0;

  struct dirent *ent;
  while ((ent = readdir(d))) {
    const char *name = ent->d_name;
    /* Ocultos, ".", "..", fantasmas "._x" do macOS e pastas de sistema. */
    if (name[0] == '.' || strcmp(name, "__MACOSX") == 0 || strncmp(name, "$RECYCLE", 8) == 0 ||
        strcmp(name, "System Volume Information") == 0)
      continue;

    char full[1024];
    snprintf(full, sizeof full, "%s/%s", path, name);
    int is_dir = ent->d_type == DT_DIR;
    if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK) {
      struct stat st;
      if (stat(full, &st) != 0) continue;
      is_dir = S_ISDIR(st.st_mode);
    }
    if (!is_dir && !is_song_name(name)) continue;

    bentry_t *e = add_entry(b);
    if (!e) break;
    snprintf(e->label, sizeof e->label, "%s", name);
    snprintf(e->path, sizeof e->path, "%s", full);
    e->is_dir = is_dir;
  }
  closedir(d);
  qsort(b->entries, (size_t)b->count, sizeof *b->entries, compare_entries);
  printf("[browser] %s: %d itens\n", path, b->count);
  return 0;
}

int browser_up(browser_t *b) {
  if (!b->path[0]) return -1;
  if (strcmp(b->path, b->root_path) == 0) {
    browser_open_root(b);
    return 0;
  }
  char parent[1024], came_from[1024];
  snprintf(came_from, sizeof came_from, "%s", b->path);
  snprintf(parent, sizeof parent, "%s", b->path);
  char *slash = strrchr(parent, '/');
  if (!slash || slash == parent) {
    browser_open_root(b);
    return 0;
  }
  *slash = '\0';
  if (browser_open(b, parent) != 0) {
    browser_open_root(b);
    return 0;
  }
  /* Deixa selecionada a pasta de onde viemos. */
  for (int i = 0; i < b->count; i++) {
    if (strcmp(b->entries[i].path, came_from) == 0) {
      b->sel = i;
      break;
    }
  }
  return 0;
}

void browser_move(browser_t *b, int delta) {
  if (b->count == 0) return;
  int sel = b->sel + delta;
  if (sel < 0) sel = delta < -1 ? 0 : b->count - 1;              /* ↑ no topo dá a volta */
  if (sel >= b->count) sel = delta > 1 ? b->count - 1 : 0;       /* ↓ no fim dá a volta */
  b->sel = sel;
}

const bentry_t *browser_selected(const browser_t *b) {
  return b->count > 0 ? &b->entries[b->sel] : NULL;
}

int browser_is_root(const browser_t *b) {
  return b->path[0] == '\0';
}
