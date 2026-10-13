/* PlayKaraoke PS5 — abre um ZIP de karaokê (MP3+G). Ver kzip.h. */

#include "kzip.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <minizip/unzip.h>

#define MAX_NAME 512

static const char *const AUDIO_EXTENSIONS[] = {".mp3", ".wav", ".ogg", ".m4a"};

static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lx = strlen(suffix);
  return ls >= lx && strcasecmp(s + ls - lx, suffix) == 0;
}

static int is_junk(const char *path) {
  const char *base = base_name(path);
  return strncasecmp(path, "__MACOSX/", 9) == 0 || strstr(path, "/__MACOSX/") != NULL ||
         strncmp(base, "._", 2) == 0 || strcmp(base, ".DS_Store") == 0 ||
         strcasecmp(base, "thumbs.db") == 0;
}

static const char *audio_extension(const char *name) {
  for (size_t i = 0; i < sizeof AUDIO_EXTENSIONS / sizeof AUDIO_EXTENSIONS[0]; i++)
    if (ends_with(name, AUDIO_EXTENSIONS[i])) return AUDIO_EXTENSIONS[i];
  return NULL;
}

/* Nome sem pasta e sem extensão, em minúsculas (para comparar .cdg x áudio). */
static void stem(const char *path, char *out, size_t size) {
  snprintf(out, size, "%s", base_name(path));
  char *dot = strrchr(out, '.');
  if (dot) *dot = '\0';
  for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
}

/* Lê a entrada atual inteira. */
static uint8_t *read_current(unzFile zf, size_t size) {
  uint8_t *buf = malloc(size ? size : 1);
  if (!buf) return NULL;
  if (unzOpenCurrentFile(zf) != UNZ_OK) {
    free(buf);
    return NULL;
  }
  size_t have = 0;
  while (have < size) {
    unsigned chunk = (size - have) > (1u << 30) ? (1u << 30) : (unsigned)(size - have);
    int n = unzReadCurrentFile(zf, buf + have, chunk);
    if (n <= 0) break;
    have += (size_t)n;
  }
  int crc_ok = unzCloseCurrentFile(zf) == UNZ_OK;
  if (have != size || !crc_ok) {
    free(buf);
    return NULL;
  }
  return buf;
}

int kzip_extract(const char *zip_path, uint8_t **cdg, size_t *cdg_size,
                 const char *audio_path, char *audio_out, size_t audio_out_size) {
  unzFile zf = unzOpen64(zip_path);
  if (!zf) {
    printf("[kzip] nao abriu %s\n", zip_path);
    return -1;
  }

  char cdg_name[MAX_NAME] = "", audio_name[MAX_NAME] = "", fallback_audio[MAX_NAME] = "";
  char name[MAX_NAME];
  unz_file_info64 info;

  /* 1ª passada: escolhe o .cdg. */
  for (int rc = unzGoToFirstFile(zf); rc == UNZ_OK; rc = unzGoToNextFile(zf)) {
    if (unzGetCurrentFileInfo64(zf, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) continue;
    if (is_junk(name) || ends_with(name, "/")) continue;
    if (!cdg_name[0] && ends_with(name, ".cdg")) snprintf(cdg_name, sizeof cdg_name, "%s", name);
  }

  /* 2ª passada: áudio de mesmo nome do .cdg, senão o primeiro. */
  char cdg_stem[MAX_NAME], s[MAX_NAME];
  stem(cdg_name, cdg_stem, sizeof cdg_stem);
  for (int rc = unzGoToFirstFile(zf); rc == UNZ_OK; rc = unzGoToNextFile(zf)) {
    if (unzGetCurrentFileInfo64(zf, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) continue;
    if (is_junk(name) || !audio_extension(name)) continue;
    if (!fallback_audio[0]) snprintf(fallback_audio, sizeof fallback_audio, "%s", name);
    stem(name, s, sizeof s);
    if (cdg_name[0] && strcmp(s, cdg_stem) == 0) {
      snprintf(audio_name, sizeof audio_name, "%s", name);
      break;
    }
  }
  if (!audio_name[0]) snprintf(audio_name, sizeof audio_name, "%s", fallback_audio);

  if (!cdg_name[0] || !audio_name[0]) {
    printf("[kzip] o ZIP precisa ter um .cdg e um audio (.mp3/.wav/.ogg/.m4a)\n");
    unzClose(zf);
    return -1;
  }
  printf("[kzip] cdg=%s audio=%s\n", cdg_name, audio_name);

  /* .cdg para a memória. */
  if (unzLocateFile(zf, cdg_name, 1) != UNZ_OK ||
      unzGetCurrentFileInfo64(zf, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
      !(*cdg = read_current(zf, (size_t)info.uncompressed_size))) {
    printf("[kzip] falha ao ler %s\n", cdg_name);
    unzClose(zf);
    return -1;
  }
  *cdg_size = (size_t)info.uncompressed_size;

  /* Áudio para um arquivo (o FFmpeg abre pelo caminho). */
  uint8_t *audio = NULL;
  if (unzLocateFile(zf, audio_name, 1) != UNZ_OK ||
      unzGetCurrentFileInfo64(zf, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
      !(audio = read_current(zf, (size_t)info.uncompressed_size))) {
    printf("[kzip] falha ao ler %s\n", audio_name);
    goto fail;
  }
  unzClose(zf);
  zf = NULL;

  snprintf(audio_out, audio_out_size, "%s%s", audio_path, audio_extension(audio_name));
  FILE *f = fopen(audio_out, "wb");
  size_t audio_size = (size_t)info.uncompressed_size;
  int ok = f && fwrite(audio, 1, audio_size, f) == audio_size;
  if (f && fclose(f) != 0) ok = 0;
  free(audio);
  if (!ok) {
    printf("[kzip] falha ao gravar %s\n", audio_out);
    goto fail;
  }
  return 0;

fail:
  if (zf) unzClose(zf);
  free(*cdg);
  *cdg = NULL;
  return -1;
}
