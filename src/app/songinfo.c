/* PlayKaraoke PS5 — informações da música pelo nome do arquivo. Ver songinfo.h. */

#include "songinfo.h"

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_PARTS 8

/* Prefixos de código das produtoras conhecidas (mesma lista do web player).
 *   SC = Sound Choice · PLK/PLKM = PlayKaraoke · EJBg/EJBv = Essential Jam Box
 *   KV/KVD = Karaoke Version · ZOOM = Zoom · RAF = RAF Electronics
 *   SF = Sunfly · STG = Stingray · SBI = SBI */
static const char KNOWN_CODE[] =
  "^(SC|PLKM?|EJB[GV]|KVD?|ZOOM|RAF|SF|STG|SBI)[A-Z]{0,4}[-_ ]?[0-9]{1,7}([-_ ][0-9]{1,4})?[A-Z]?$";
/* Produtoras não listadas: letras curtas + números, faixa opcional. */
static const char GENERIC_CODE[] = "^[A-Za-z]{1,8}-?[0-9]{2,8}(-[0-9]{1,4})?$";

static int matches(const char *pattern, int flags, const char *s) {
  regex_t re;
  if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB | flags) != 0) return 0;
  int ok = regexec(&re, s, 0, NULL, 0) == 0;
  regfree(&re);
  return ok;
}

static int is_known_code(const char *s) { return matches(KNOWN_CODE, REG_ICASE, s); }
static int looks_like_code(const char *s) { return is_known_code(s) || matches(GENERIC_CODE, 0, s); }

static void trim(char *s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
  size_t start = 0;
  while (isspace((unsigned char)s[start])) start++;
  if (start) memmove(s, s + start, len - start + 1);
}

/* Divide em pedaços pelo separador " - " (espaços em volta do hífen). */
static int split_parts(const char *clean, char parts[MAX_PARTS][256]) {
  int count = 0;
  const char *start = clean;
  for (const char *p = clean; ; p++) {
    int at_end = *p == '\0';
    int is_sep = !at_end && *p == '-' && p > clean && isspace((unsigned char)p[-1]) &&
                 isspace((unsigned char)p[1]);
    if (!at_end && !is_sep) continue;
    if (count < MAX_PARTS) {
      size_t len = (size_t)(p - start);
      if (len > 255) len = 255;
      memcpy(parts[count], start, len);
      parts[count][len] = '\0';
      trim(parts[count]);
      if (parts[count][0]) count++;
    }
    if (at_end) break;
    start = p + 1;
  }
  return count;
}

/* Junta os pedaços [from, count) de volta com " - " (títulos com hífen). */
static void join_parts(char parts[MAX_PARTS][256], int from, int count, char *out, size_t size) {
  out[0] = '\0';
  for (int i = from; i < count; i++) {
    if (i > from) strncat(out, " - ", size - strlen(out) - 1);
    strncat(out, parts[i], size - strlen(out) - 1);
  }
}

void songinfo_parse(const char *filename, songinfo_t *out) {
  memset(out, 0, sizeof *out);

  char clean[512];
  snprintf(clean, sizeof clean, "%s", filename);
  size_t len = strlen(clean);
  if (len > 4 && (strcasecmp(clean + len - 4, ".zip") == 0 || strcasecmp(clean + len - 4, ".mp4") == 0))
    clean[len - 4] = '\0';
  trim(clean);

  char parts[MAX_PARTS][256];
  int count = split_parts(clean, parts);

  if (count >= 3 && looks_like_code(parts[0])) {
    snprintf(out->code, sizeof out->code, "%s", parts[0]);
    snprintf(out->artist, sizeof out->artist, "%s", parts[1]);
    join_parts(parts, 2, count, out->title, sizeof out->title);
  } else if (count == 2 && is_known_code(parts[0]) && !strchr(parts[0], ' ')) {
    /* "CÓDIGO - Música": só com prefixo conhecido e sem espaço no código,
     * para não confundir com "Artista 2000 - Música". */
    snprintf(out->code, sizeof out->code, "%s", parts[0]);
    snprintf(out->title, sizeof out->title, "%s", parts[1]);
  } else if (count >= 2) {
    snprintf(out->artist, sizeof out->artist, "%s", parts[0]);
    join_parts(parts, 1, count, out->title, sizeof out->title);
  } else {
    snprintf(out->title, sizeof out->title, "%s", clean[0] ? clean : filename);
  }
}

void songinfo_display(const songinfo_t *info, char *out, size_t size) {
  if (info->artist[0]) snprintf(out, size, "%s \xE2\x80\x93 %s", info->artist, info->title);
  else snprintf(out, size, "%s", info->title);
}
