/* PlayKaraoke PS5 — servidor web para os celulares. Ver server.h. */

#include "server.h"
#include "i18n.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <microhttpd.h>
#include <SDL.h>

#include "online.h"
#include "songinfo.h"

#define SEARCH_MAX 60
#define CMD_TIMEOUT_SEC 5

struct server {
  struct MHD_Daemon *daemon;
  library_t *lib;
  const uint8_t *page, *yt_page;
  size_t page_size, yt_page_size;
  void (*wake)(void *ctx);
  void *wake_ctx;

  pthread_mutex_t lock;
  pthread_cond_t cond;
  char *state; /* último retrato publicado */
  server_cmd_t *head, *tail;
};

/* ---------- JSON ---------- */

void json_append_string(char *out, size_t size, const char *text) {
  size_t o = strlen(out);
  if (o + 1 >= size) return;
  out[o++] = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p && o + 8 < size; p++) {
    switch (*p) {
      case '"': out[o++] = '\\'; out[o++] = '"'; break;
      case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
      case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
      case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
      case '\t': out[o++] = '\\'; out[o++] = 't'; break;
      default:
        if (*p < 0x20) o += (size_t)snprintf(out + o, size - o, "\\u%04x", *p);
        else out[o++] = (char)*p;
    }
  }
  if (o + 1 < size) out[o++] = '"';
  out[o] = '\0';
}

void server_client_tag(const char *client, char out[9]) {
  uint32_t h = 2166136261u; /* FNV-1a */
  for (const unsigned char *p = (const unsigned char *)client; *p; p++) h = (h ^ *p) * 16777619u;
  snprintf(out, 9, "%08x", h);
}

/* ---------- Respostas ---------- */

static enum MHD_Result reply(struct MHD_Connection *conn, unsigned status, const char *type,
                             const void *data, size_t size, enum MHD_ResponseMemoryMode mode) {
  struct MHD_Response *r = MHD_create_response_from_buffer(size, (void *)data, mode);
  if (!r) return MHD_NO;
  MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE, type);
  MHD_add_response_header(r, MHD_HTTP_HEADER_CACHE_CONTROL, "no-store");
  enum MHD_Result ret = MHD_queue_response(conn, status, r);
  MHD_destroy_response(r);
  return ret;
}

static enum MHD_Result reply_json(struct MHD_Connection *conn, unsigned status, const char *json) {
  char *copy = strdup(json);
  if (!copy) return MHD_NO;
  return reply(conn, status, "application/json; charset=utf-8", copy, strlen(copy), MHD_RESPMEM_MUST_FREE);
}

static const char *arg(struct MHD_Connection *conn, const char *name) {
  const char *v = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, name);
  return v ? v : "";
}

/* ---------- Rotas ---------- */

static enum MHD_Result handle_state(server_t *s, struct MHD_Connection *conn) {
  pthread_mutex_lock(&s->lock);
  char *copy = strdup(s->state ? s->state : "{}");
  pthread_mutex_unlock(&s->lock);
  if (!copy) return MHD_NO;
  return reply(conn, 200, "application/json; charset=utf-8", copy, strlen(copy), MHD_RESPMEM_MUST_FREE);
}

/* Busca no YouTube (bloqueia esta conexão ~1 s, as outras seguem). */
static enum MHD_Result handle_online_search(struct MHD_Connection *conn, const char *q) {
  online_result_t results[ONLINE_MAX_RESULTS];
  char error[256];
  int n = q[0] ? online_search(q, results, ONLINE_MAX_RESULTS, error, sizeof error) : 0;
  if (n < 0) {
    char body[400] = "{\"error\":";
    json_append_string(body, sizeof body, error);
    strncat(body, "}", sizeof body - strlen(body) - 1);
    return reply_json(conn, 502, body);
  }
  size_t size = 128 + (size_t)n * 1200;
  char *json = malloc(size);
  if (!json) return MHD_NO;
  snprintf(json, size, "{\"total\":%d,\"results\":[", n);
  for (int i = 0; i < n; i++) {
    songinfo_t info;
    online_song_info(results[i].title, &info);
    char path[48];
    snprintf(path, sizeof path, ONLINE_PREFIX "%s", results[i].video_id);
    strncat(json, i ? ",{\"path\":" : "{\"path\":", size - strlen(json) - 1);
    json_append_string(json, size, path);
    strncat(json, ",\"video\":", size - strlen(json) - 1);
    json_append_string(json, size, results[i].title);
    strncat(json, ",\"artist\":", size - strlen(json) - 1);
    json_append_string(json, size, info.artist);
    strncat(json, ",\"title\":", size - strlen(json) - 1);
    json_append_string(json, size, info.title);
    strncat(json, ",\"code\":", size - strlen(json) - 1);
    json_append_string(json, size, results[i].channel);
    strncat(json, ",\"thumb\":", size - strlen(json) - 1);
    json_append_string(json, size, results[i].thumb);
    strncat(json, ",\"format\":\"YouTube\"}", size - strlen(json) - 1);
  }
  strncat(json, "]}", size - strlen(json) - 1);
  return reply(conn, 200, "application/json; charset=utf-8", json, strlen(json), MHD_RESPMEM_MUST_FREE);
}

static enum MHD_Result handle_search(server_t *s, struct MHD_Connection *conn) {
  const char *q = arg(conn, "q");
  if (strcmp(arg(conn, "src"), "yt") == 0) return handle_online_search(conn, q);
  int f = atoi(arg(conn, "f"));
  if (f < 0 || f > 2) f = 0;
  char (*results)[1024] = malloc(SEARCH_MAX * sizeof *results);
  if (!results) return MHD_NO;
  int total = 0;
  int n = library_search(s->lib, q, (library_filter_t)f, results, SEARCH_MAX, &total);

  size_t size = 256 + (size_t)n * 1600;
  char *json = malloc(size);
  if (!json) {
    free(results);
    return MHD_NO;
  }
  snprintf(json, size, "{\"total\":%d,\"scanning\":%s,\"results\":[", total,
           s->lib->scanning ? "true" : "false");
  for (int i = 0; i < n; i++) {
    const char *slash = strrchr(results[i], '/');
    songinfo_t info;
    songinfo_parse(slash ? slash + 1 : results[i], &info);
    size_t len = strlen(results[i]);
    int mp4 = len > 4 && strcasecmp(results[i] + len - 4, ".mp4") == 0;
    strncat(json, i ? ",{\"path\":" : "{\"path\":", size - strlen(json) - 1);
    json_append_string(json, size, results[i]);
    strncat(json, ",\"artist\":", size - strlen(json) - 1);
    json_append_string(json, size, info.artist);
    strncat(json, ",\"title\":", size - strlen(json) - 1);
    json_append_string(json, size, info.title);
    strncat(json, ",\"code\":", size - strlen(json) - 1);
    json_append_string(json, size, info.code);
    strncat(json, mp4 ? ",\"format\":\"MP4\"}" : ",\"format\":\"CDG\"}", size - strlen(json) - 1);
  }
  strncat(json, "]}", size - strlen(json) - 1);
  free(results);
  return reply(conn, 200, "application/json; charset=utf-8", json, strlen(json), MHD_RESPMEM_MUST_FREE);
}

/* Entrega o comando ao app e espera a resposta (até CMD_TIMEOUT_SEC). */
static enum MHD_Result run_command(server_t *s, struct MHD_Connection *conn, server_cmd_t *cmd) {
  pthread_mutex_lock(&s->lock);
  if (s->tail) s->tail->next = cmd;
  else s->head = cmd;
  s->tail = cmd;
  pthread_mutex_unlock(&s->lock);
  if (s->wake) s->wake(s->wake_ctx);

  struct timespec until;
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += CMD_TIMEOUT_SEC;
  pthread_mutex_lock(&s->lock);
  while (!cmd->done) {
    if (pthread_cond_timedwait(&s->cond, &s->lock, &until) == ETIMEDOUT) break;
  }
  if (!cmd->done) {
    cmd->abandoned = 1; /* o app libera quando terminar */
    pthread_mutex_unlock(&s->lock);
    return reply_json(conn, 503, "{\"error\":\"The PS5 is busy, try again.\"}");
  }
  pthread_mutex_unlock(&s->lock);
  enum MHD_Result r = reply_json(conn, (unsigned)cmd->status, cmd->body);
  free(cmd);
  return r;
}

static enum MHD_Result handle_command(server_t *s, struct MHD_Connection *conn, server_cmd_type_t type) {
  server_cmd_t *cmd = calloc(1, sizeof *cmd);
  if (!cmd) return MHD_NO;
  cmd->type = type;
  snprintf(cmd->client, sizeof cmd->client, "%s", arg(conn, "client"));
  snprintf(cmd->name, sizeof cmd->name, "%s", arg(conn, "name"));
  snprintf(cmd->path, sizeof cmd->path, "%s", arg(conn, "path"));
  snprintf(cmd->title, sizeof cmd->title, "%s", arg(conn, "title"));
  cmd->song_id = atoi(arg(conn, "id"));
  cmd->semitones = atoi(arg(conn, "semitones"));
  if (!cmd->client[0]) {
    free(cmd);
    return reply_json(conn, 400, "{\"error\":\"Phone without an ID.\"}");
  }
  return run_command(s, conn, cmd);
}

#ifdef PK_DEV_TOOLS
/* Simula um botão do controle (ou texto do teclado do PS5) para o app. */
static enum MHD_Result handle_dev_input(struct MHD_Connection *conn) {
  static const struct { const char *name; SDL_GameControllerButton button; } keys[] = {
    {"cross", SDL_CONTROLLER_BUTTON_A},          {"circle", SDL_CONTROLLER_BUTTON_B},
    {"square", SDL_CONTROLLER_BUTTON_X},         {"triangle", SDL_CONTROLLER_BUTTON_Y},
    {"options", SDL_CONTROLLER_BUTTON_BACK},     {"touch", SDL_CONTROLLER_BUTTON_START},
    {"l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},  {"r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"up", SDL_CONTROLLER_BUTTON_DPAD_UP},       {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT},   {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
  };
  const char *k = arg(conn, "k"), *text = arg(conn, "text");
  SDL_Event e;
  if (text[0]) {
    SDL_zero(e);
    e.type = SDL_TEXTINPUT;
    snprintf(e.text.text, sizeof e.text.text, "%s", text);
    SDL_PushEvent(&e);
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_RETURN;
    SDL_PushEvent(&e);
    return reply_json(conn, 200, "{\"ok\":true}");
  }
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
    if (strcmp(k, keys[i].name) != 0) continue;
    SDL_zero(e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = (Uint8)keys[i].button;
    SDL_PushEvent(&e);
    e.type = SDL_CONTROLLERBUTTONUP;
    SDL_PushEvent(&e);
    return reply_json(conn, 200, "{\"ok\":true}");
  }
  return reply_json(conn, 400, "{\"error\":\"unknown key\"}");
}
#endif

static enum MHD_Result on_request(void *cls, struct MHD_Connection *conn, const char *url,
                                  const char *method, const char *version, const char *upload_data,
                                  size_t *upload_data_size, void **req_cls) {
  (void)version;
  (void)upload_data;
  server_t *s = cls;
  /* 1ª chamada: só os cabeçalhos chegaram. */
  static int marker;
  if (!*req_cls) {
    *req_cls = &marker;
    return MHD_YES;
  }
  if (*upload_data_size) { /* corpo ignorado: os parâmetros vêm na URL */
    *upload_data_size = 0;
    return MHD_YES;
  }

  int is_get = strcmp(method, "GET") == 0, is_post = strcmp(method, "POST") == 0;
  if (is_get && (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0))
    return reply(conn, 200, "text/html; charset=utf-8", s->page, s->page_size, MHD_RESPMEM_PERSISTENT);
  if (is_get && strcmp(url, "/yt") == 0)
    return reply(conn, 200, "text/html; charset=utf-8", s->yt_page, s->yt_page_size, MHD_RESPMEM_PERSISTENT);
  if (is_get && strcmp(url, "/api/state") == 0) return handle_state(s, conn);
  if (is_post && strcmp(url, "/api/ytlog") == 0) { /* diagnóstico do player do YouTube */
    printf("[yt] video=%s %s\n", arg(conn, "v"), arg(conn, "e"));
    return reply_json(conn, 200, "{\"ok\":true}");
  }
  if (is_get && strcmp(url, "/api/search") == 0) return handle_search(s, conn);
  if (is_post && strcmp(url, "/api/queue") == 0) return handle_command(s, conn, CMD_ADD);
  if (is_post && strcmp(url, "/api/cancel") == 0) return handle_command(s, conn, CMD_CANCEL);
  if (is_post && strcmp(url, "/api/pitch") == 0) return handle_command(s, conn, CMD_PITCH);
  if (is_post && strcmp(url, "/api/finish") == 0) return handle_command(s, conn, CMD_FINISH);
#ifdef PK_DEV_TOOLS
  if (is_post && strcmp(url, "/api/dev/input") == 0) return handle_dev_input(conn);
  if (is_post && strcmp(url, "/api/dev/shot") == 0) {
    server_cmd_t *cmd = calloc(1, sizeof *cmd);
    if (!cmd) return MHD_NO;
    cmd->type = CMD_SHOT;
    return run_command(s, conn, cmd);
  }
#endif
  return reply_json(conn, 404, "{\"error\":\"not_found\"}");
}

/* ---------- Ciclo de vida ---------- */

server_t *server_start(int port, library_t *lib, const uint8_t *page, size_t page_size,
                       const uint8_t *yt_page, size_t yt_page_size, void (*wake)(void *ctx), void *wake_ctx) {
  server_t *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->lib = lib;
  s->page = page;
  s->page_size = page_size;
  s->yt_page = yt_page;
  s->yt_page_size = yt_page_size;
  s->wake = wake;
  s->wake_ctx = wake_ctx;
  pthread_mutex_init(&s->lock, NULL);
  pthread_cond_init(&s->cond, NULL);
  s->daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION,
                               (uint16_t)port, NULL, NULL, on_request, s,
                               MHD_OPTION_CONNECTION_TIMEOUT, (unsigned)30, MHD_OPTION_END);
  if (!s->daemon) {
    printf("[server] nao abriu a porta %d\n", port);
    server_stop(s);
    return NULL;
  }
  printf("[server] ouvindo na porta %d\n", port);
  return s;
}

void server_stop(server_t *s) {
  if (!s) return;
  if (s->daemon) MHD_stop_daemon(s->daemon);
  for (server_cmd_t *c = s->head, *n; c; c = n) {
    n = c->next;
    free(c);
  }
  free(s->state);
  pthread_cond_destroy(&s->cond);
  pthread_mutex_destroy(&s->lock);
  free(s);
}

void server_set_state(server_t *s, const char *json) {
  char *copy = strdup(json);
  if (!copy) return;
  pthread_mutex_lock(&s->lock);
  free(s->state);
  s->state = copy;
  pthread_mutex_unlock(&s->lock);
}

server_cmd_t *server_take(server_t *s) {
  pthread_mutex_lock(&s->lock);
  server_cmd_t *list = s->head;
  s->head = s->tail = NULL;
  pthread_mutex_unlock(&s->lock);
  return list;
}

int server_pending(server_t *s) {
  pthread_mutex_lock(&s->lock);
  int pending = s->head != NULL;
  pthread_mutex_unlock(&s->lock);
  return pending;
}

void server_done(server_t *s, server_cmd_t *cmd) {
  pthread_mutex_lock(&s->lock);
  cmd->next = NULL;
  if (cmd->abandoned) {
    pthread_mutex_unlock(&s->lock);
    free(cmd);
    return;
  }
  cmd->done = 1;
  pthread_cond_broadcast(&s->cond);
  pthread_mutex_unlock(&s->lock);
}
