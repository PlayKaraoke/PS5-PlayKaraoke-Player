/* PlayKaraoke PS5 — busca online (YouTube). Ver online.h. */

#include "online.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>

#include "i18n.h"

#define ENDPOINT "https://playkaraoke-search.playkaraoke.workers.dev/search"
#define ORIGIN "https://playkaraoke.github.io"
#define MAX_BODY (256 * 1024)

/* Certificados confiáveis (Mozilla, via curl.se): o PS5 não tem uma lista
 * acessível ao homebrew. Embutidos pelo Makefile (CACERT_FILE). */
#ifndef CACERT_FILE
#error "CACERT_FILE não definido"
#endif
__asm__(".section .rodata\n"
        ".global online_cacert\nonline_cacert:\n"
        ".incbin \"" CACERT_FILE "\"\nonline_cacert_end:\n"
        ".global online_cacert_size\nonline_cacert_size:\n"
        ".quad online_cacert_end - online_cacert\n"
        ".previous\n");
extern const char online_cacert[];
extern const unsigned long online_cacert_size;

static char g_base_url[128];

void online_init(const char *base_url) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  snprintf(g_base_url, sizeof g_base_url, "%s", base_url);
}

/* ---------- JSON mínimo (só o formato do Worker) ---------- */

static void put_utf8(char *out, size_t *o, size_t size, unsigned cp) {
  char buf[4];
  int n;
  if (cp < 0x80) {
    buf[0] = (char)cp;
    n = 1;
  } else if (cp < 0x800) {
    buf[0] = (char)(0xC0 | cp >> 6);
    buf[1] = (char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | cp >> 12);
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    buf[0] = (char)(0xF0 | cp >> 18);
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    n = 4;
  }
  for (int i = 0; i < n && *o + 1 < size; i++) out[(*o)++] = buf[i];
}

static unsigned hex4(const char *p) {
  char h[5] = {0};
  for (int i = 0; i < 4 && p[i]; i++) h[i] = p[i];
  return (unsigned)strtoul(h, NULL, 16);
}

/* Lê a string JSON que começa em `p` (na aspa) para `out`. */
static void read_string(const char *p, char *out, size_t size) {
  size_t o = 0;
  if (*p == '"') p++;
  for (; *p && *p != '"'; p++) {
    if (*p != '\\') {
      if (o + 1 < size) out[o++] = *p;
      continue;
    }
    p++;
    if (*p == '\0') break;
    if (*p == 'u') {
      unsigned cp = hex4(p + 1);
      p += 4;
      if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u') { /* par substituto */
        cp = 0x10000 + ((cp - 0xD800) << 10) + (hex4(p + 3) - 0xDC00);
        p += 6;
      }
      put_utf8(out, &o, size, cp);
    } else if (o + 1 < size) {
      out[o++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; /* \" \\ \/ */
    }
  }
  out[o] = '\0';
}

/* Valor (string) da chave `key` dentro do objeto de `obj` até `end`. */
static void object_string(const char *obj, const char *end, const char *key, char *out, size_t size) {
  char pattern[48];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);
  out[0] = '\0';
  for (const char *p = obj; (p = strstr(p, pattern)) && p < end; p++) {
    const char *v = p + strlen(pattern);
    while (*v == ' ' || *v == ':') v++;
    if (*v == '"') {
      read_string(v, out, size);
      return;
    }
  }
}

/* Fim do objeto JSON que começa em `p` ('{'), respeitando strings. */
static const char *object_end(const char *p) {
  int depth = 0, in_str = 0;
  for (; *p; p++) {
    if (in_str) {
      if (*p == '\\' && p[1]) p++;
      else if (*p == '"') in_str = 0;
    } else if (*p == '"') {
      in_str = 1;
    } else if (*p == '{') {
      depth++;
    } else if (*p == '}' && --depth == 0) {
      return p;
    }
  }
  return NULL;
}

/* ---------- HTTP ---------- */

typedef struct body {
  char *data;
  size_t size, max;
} body_t;

static size_t on_data(char *ptr, size_t size, size_t n, void *user) {
  body_t *b = user;
  size_t len = size * n;
  if (b->size + len >= b->max) return 0;
  memcpy(b->data + b->size, ptr, len);
  b->size += len;
  b->data[b->size] = '\0';
  return len;
}

int online_search(const char *query, online_result_t *out, int max, char *error, size_t error_size) {
  error[0] = '\0';
  CURL *curl = curl_easy_init();
  body_t body = {malloc(MAX_BODY), 0, MAX_BODY};
  if (!curl || !body.data) {
    snprintf(error, error_size, "%s", TR("Online search is unavailable.", "Busca online indisponível."));
    free(body.data);
    if (curl) curl_easy_cleanup(curl);
    return -1;
  }
  body.data[0] = '\0';

  char *escaped = curl_easy_escape(curl, query, 0);
  char url[1024];
  snprintf(url, sizeof url, "%s?q=%s", ENDPOINT, escaped ? escaped : "");
  curl_free(escaped);

  struct curl_slist *headers = curl_slist_append(NULL, "Origin: " ORIGIN);
  struct curl_blob ca = {(void *)online_cacert, online_cacert_size, CURL_BLOB_NOCOPY};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "PlayKaraoke-PS5");

  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  int found = -1;
  if (rc != CURLE_OK) {
    printf("[online] %s\n", curl_easy_strerror(rc));
    snprintf(error, error_size, "%s",
             TR("No internet connection on the PS5.", "O PS5 está sem conexão com a internet."));
  } else if (status == 429 || strstr(body.data, "\"quota\"")) {
    snprintf(error, error_size, "%s",
             TR("YouTube's daily search limit was reached. Try again tomorrow or search the USB drive.",
                "O limite diário de buscas do YouTube acabou. Tente amanhã ou busque no HD."));
  } else if (status != 200 || !strstr(body.data, "\"results\"")) {
    printf("[online] HTTP %ld\n", status);
    snprintf(error, error_size, "%s", TR("Online search failed, try again.", "A busca online falhou, tente de novo."));
  } else {
    found = 0;
    const char *p = strstr(body.data, "\"results\"");
    p = p ? strchr(p, '[') : NULL;
    while (p && found < max && (p = strchr(p, '{'))) {
      const char *end = object_end(p);
      if (!end) break;
      online_result_t *r = &out[found];
      object_string(p, end, "videoId", r->video_id, sizeof r->video_id);
      object_string(p, end, "title", r->title, sizeof r->title);
      object_string(p, end, "channel", r->channel, sizeof r->channel);
      object_string(p, end, "thumbnail", r->thumb, sizeof r->thumb);
      char path[48];
      snprintf(path, sizeof path, ONLINE_PREFIX "%s", r->video_id);
      if (online_is_path(path) && r->title[0]) found++;
      p = end + 1;
    }
  }
  free(body.data);
  return found;
}

int online_fetch(const char *url, unsigned char **data, size_t *size, size_t max_size) {
  *data = NULL;
  *size = 0;
  if (strncmp(url, "https://", 8) != 0) return -1;
  CURL *curl = curl_easy_init();
  body_t body = {malloc(max_size + 1), 0, max_size + 1};
  if (!curl || !body.data) {
    free(body.data);
    if (curl) curl_easy_cleanup(curl);
    return -1;
  }
  struct curl_blob ca = {(void *)online_cacert, online_cacert_size, CURL_BLOB_NOCOPY};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK || status != 200 || body.size == 0) {
    free(body.data);
    return -1;
  }
  *data = (unsigned char *)body.data;
  *size = body.size;
  return 0;
}

/* ---------- Fila e reprodução ---------- */

int online_is_path(const char *path) {
  if (strncmp(path, ONLINE_PREFIX, strlen(ONLINE_PREFIX)) != 0) return 0;
  const char *id = path + strlen(ONLINE_PREFIX);
  size_t len = strlen(id);
  if (len < 6 || len > 20) return 0;
  for (size_t i = 0; i < len; i++)
    if (!isalnum((unsigned char)id[i]) && id[i] != '-' && id[i] != '_') return 0;
  return 1;
}

/* Tira "(Karaoke Version)", "[Karaokê]", "Karaoke" e afins. */
static void strip_karaoke(char *s) {
  for (char *k; (k = strcasestr(s, "karaok"));) {
    char *start = k, *end = k + 6;
    if (*end == 'e' || *end == 'E') end++;
    else if ((unsigned char)end[0] == 0xC3 && ((unsigned char)end[1] == 0xAA || (unsigned char)end[1] == 0x8A)) end += 2;
    while (*end == ' ') end++;
    if (strncasecmp(end, "version", 7) == 0) end += 7;
    else if (strncasecmp(end, "vers\xC3\xA3o", 7) == 0) end += 7;
    while (*end == ' ' || *end == ')' || *end == ']') end++;
    while (start > s && (start[-1] == ' ' || start[-1] == '(' || start[-1] == '[')) start--;
    memmove(start, end, strlen(end) + 1);
    if (start > s && *start) { /* mantém um espaço entre as palavras */
      memmove(start + 1, start, strlen(start) + 1);
      *start = ' ';
    }
  }
  /* Espaços repetidos e nas pontas. */
  char *w = s;
  for (char *r = s; *r; r++)
    if (!(*r == ' ' && (w == s || w[-1] == ' '))) *w++ = *r;
  while (w > s && w[-1] == ' ') w--;
  *w = '\0';
}

void online_song_info(const char *video_title, songinfo_t *out) {
  memset(out, 0, sizeof *out);
  char clean[256];
  snprintf(clean, sizeof clean, "%s", video_title);
  strip_karaoke(clean);
  if (!clean[0]) snprintf(clean, sizeof clean, "%s", video_title);

  /* "Artista - Música": primeiro separador " - ", " – ", " — " ou " | ". */
  static const char *const seps[] = {" - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", " | "};
  char *cut = NULL;
  size_t cut_len = 0;
  for (size_t i = 0; i < sizeof seps / sizeof seps[0]; i++) {
    char *at = strstr(clean, seps[i]);
    if (at && (!cut || at < cut)) {
      cut = at;
      cut_len = strlen(seps[i]);
    }
  }
  if (cut) {
    *cut = '\0';
    snprintf(out->artist, sizeof out->artist, "%s", clean);
    snprintf(out->title, sizeof out->title, "%s", cut + cut_len);
  } else {
    snprintf(out->title, sizeof out->title, "%s", clean);
  }
  snprintf(out->code, sizeof out->code, "YouTube");
}

void online_watch_url(const char *path, char *out, size_t size) {
  snprintf(out, size, "%s/yt?v=%s", g_base_url, path + strlen(ONLINE_PREFIX));
}
