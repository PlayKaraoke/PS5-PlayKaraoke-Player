/* PlayKaraoke PS5 — app (roda como BigApp, aberto pelo launcher).
 *
 * Máquina de estados das telas da TV (PLANO.md, seção 6):
 *   OCIOSO    logo, fila e "a seguir"   ✕ começar, △ buscar, ▢ pastas, ○ sair
 *   BUSCA     teclado do PS5 + resultados ✕ adicionar/tirar da fila,
 *                                        ▢ iniciar show, △ nova busca,
 *                                        R1 formato, ○ voltar
 *   PASTAS    pastas do HD               ✕ abrir/adicionar/tirar,
 *                                        ▢ iniciar show, ○ voltar
 *   CONTAGEM  próximo cantor e contagem  ✕ começar já, ○ pular a vez,
 *                                        △ buscar
 *   TOCANDO   karaokê em tela cheia      (ver playback.h)
 *   FILA      rotação e músicas          △ remover, ○ fechar
 * OPTIONS abre a FILA em qualquer tela, inclusive com música tocando.
 * As músicas escolhidas pelo DualSense entram na rotação como o cantor "PS5";
 * os celulares pedem pelo servidor (server.h), na porta SERVER_PORT, pelo
 * endereço do QR code. Depois que o show começa, um pedido com a fila parada
 * já inicia a contagem.
 *
 * O printf vai para /data/playkaraoke/logs/app.log (o launcher redireciona). */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL2_gfxPrimitives.h>
#include <SDL_image.h>

#include "assets.h"
#include "i18n.h"
#include "browser.h"
#include "library.h"
#include "osd.h"
#include "playback.h"
#include "queue.h"
#include "queue_view.h"
#include "server.h"
#include "ui.h"
#include "../../vendor/qrcodegen/qrcodegen.h"

#define CACHE_DIR "/data/playkaraoke/cache"
#define SONGS_DIR "/data/playkaraoke/songs"
#define INDEX_FILE "/data/playkaraoke/index.txt"
#define VOLUME_DEFAULT 50 /* toda vez que o app abre */
#define COUNTDOWN_SEC 10 /* mesmo padrão do web player */
#define TOAST_MS 3000
#define SERVER_PORT 8000
#define STATE_MAX (1024 * 1024)
#define SCREENSHOT_FILE "/data/playkaraoke/cache/screen.png"

#define NAV_DELAY_MS 350 /* segurar ↑/↓ repete depois disso */
#define NAV_REPEAT_MS 80
#define STICK_ON 16000
#define STICK_OFF 12000

#define LIST_Y 200
#define ROW_H 62
#define ROWS ((UI_H - LIST_Y - 150) / ROW_H)
#define SEARCH_LIST_Y 290
#define SEARCH_ROWS ((UI_H - SEARCH_LIST_Y - 150) / ROW_H)

typedef enum {
  ST_IDLE, ST_SEARCH, ST_BROWSER, ST_COUNTDOWN, ST_PLAYING, ST_QUEUE, ST_CONFIRM_EXIT, ST_EXIT
} state_t;

typedef enum {
  ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
  ACT_CROSS, ACT_CIRCLE, ACT_TRIANGLE, ACT_SQUARE, ACT_OPTIONS, ACT_L1, ACT_R1, ACT_QUIT,
  ACT_TEXT_ENTER, /* teclado do PS5 confirmado */
} action_t;

typedef struct app {
  SDL_Window *win;
  ui_t ui;
  osd_t osd;
  queue_t queue;
  queue_view_t queue_view;
  browser_t browser;
  library_t library;
  state_t state;
  state_t back_state;  /* para onde ○ volta da busca/pastas */
  state_t queue_back;  /* para onde a FILA volta */
  int volume;
  int playing_id;      /* música tocando agora (0 = nenhuma) */
  int playing_semitones;
  int show_on;         /* o dono já iniciou o show nesta sessão */

  /* celulares */
  server_t *server;
  Uint32 wake_event;
  char url[64];        /* http://IP:porta (QR code) */
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  int qr_ok;
  char *state_json;
  char state_key[96];  /* o que o último retrato publicado refletia */

  char toast[256];
  Uint32 toast_until;
  Uint32 countdown_end;
  Uint32 countdown_left; /* ms que faltavam ao abrir a FILA na contagem */

  /* busca */
  char query[256];
  library_filter_t filter;
  char (*results)[1024];
  int result_count, result_total, result_sel, result_scroll;
  int result_library_count; /* tamanho da biblioteca na última busca */

  /* teclado do PS5 (IME): o texto chega em SDL_TEXTINPUT e o OK em Return;
   * cancelar não gera evento, só fecha o teclado */
  int ime_active, ime_seen;
  char ime_text[256];

  /* ↑/↓ com repetição (seta ou analógico esquerdo) */
  int dpad_dir, stick_dir;
  Uint32 nav_next;
} app_t;

/* ---------- Entrada ---------- */

static action_t nav_action(int dir) { return dir < 0 ? ACT_UP : ACT_DOWN; }

static action_t nav_start(app_t *a, int dir) {
  a->nav_next = SDL_GetTicks() + NAV_DELAY_MS;
  return nav_action(dir);
}

static int nav_held(const app_t *a) { return a->dpad_dir ? a->dpad_dir : a->stick_dir; }

/* Traduz um evento do SDL numa ação (ou ACT_NONE). */
static action_t translate_event(app_t *a, const SDL_Event *e) {
  switch (e->type) {
    case SDL_QUIT:
      return ACT_QUIT;
    case SDL_CONTROLLERDEVICEADDED:
      SDL_GameControllerOpen(e->cdevice.which);
      break;
    case SDL_TEXTINPUT:
      if (a->ime_active)
        strncat(a->ime_text, e->text.text, sizeof a->ime_text - strlen(a->ime_text) - 1);
      break;
    case SDL_KEYDOWN:
      if (a->ime_active && (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_KP_ENTER))
        return ACT_TEXT_ENTER;
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      if (a->ime_active) break; /* o teclado do PS5 está na frente */
      switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: return ACT_CROSS;
        case SDL_CONTROLLER_BUTTON_B: return ACT_CIRCLE;
        case SDL_CONTROLLER_BUTTON_Y: return ACT_TRIANGLE;
        case SDL_CONTROLLER_BUTTON_X: return ACT_SQUARE;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ACT_L1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ACT_R1;
        /* No SDL do PS5, Options chega como BACK (o touchpad é START). */
        case SDL_CONTROLLER_BUTTON_BACK: return ACT_OPTIONS;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ACT_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ACT_RIGHT;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: a->dpad_dir = -1; return nav_start(a, -1);
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: a->dpad_dir = 1; return nav_start(a, 1);
      }
      break;
    case SDL_CONTROLLERBUTTONUP:
      if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
          e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        a->dpad_dir = 0;
      break;
    case SDL_CONTROLLERAXISMOTION:
      if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
        int v = e->caxis.value;
        int dir = v < -STICK_ON ? -1 : v > STICK_ON ? 1 : (v > -STICK_OFF && v < STICK_OFF) ? 0 : a->stick_dir;
        if (dir != a->stick_dir) {
          a->stick_dir = dir;
          if (dir) return nav_start(a, dir);
        }
      }
      break;
  }
  return ACT_NONE;
}

/* Espera até `timeout_ms` por uma ação do controle. */
static action_t poll_action(app_t *a, int timeout_ms) {
  Uint32 now = SDL_GetTicks();
  if (nav_held(a)) {
    Sint32 left = (Sint32)(a->nav_next - now);
    if (left <= 0) {
      a->nav_next = now + NAV_REPEAT_MS;
      return nav_action(nav_held(a));
    }
    if (left < timeout_ms) timeout_ms = left;
  }

  SDL_Event e;
  if (!SDL_WaitEventTimeout(&e, timeout_ms)) return ACT_NONE;
  do {
    action_t act = translate_event(a, &e);
    if (act != ACT_NONE) return act;
  } while (SDL_PollEvent(&e));
  return ACT_NONE;
}

/* Entrada da tela TOCANDO fica com o playback: zera o estado de navegação
 * para não "vazar" uma seta segurada para a tela seguinte. */
static void reset_nav(app_t *a) {
  a->dpad_dir = a->stick_dir = 0;
}

/* ---------- Utilidades ---------- */

static void show_toast(app_t *a, const char *text) {
  snprintf(a->toast, sizeof a->toast, "%s", text);
  a->toast_until = SDL_GetTicks() + TOAST_MS;
}

static int toast_visible(const app_t *a) {
  return a->toast[0] && (Sint32)(a->toast_until - SDL_GetTicks()) > 0;
}

static int in_queue(const app_t *a, const char *path) {
  return queue_find(&a->queue, QUEUE_HOST_NAME, path) >= 0;
}

static void song_display_for_path(const char *path, char *out, size_t size) {
  const char *slash = strrchr(path, '/');
  songinfo_t info;
  songinfo_parse(slash ? slash + 1 : path, &info);
  songinfo_display(&info, out, size);
}

/* ✕ numa música: entra na fila (cantor "PS5"); se já estava, sai. `info`
 * vem pronto nos vídeos do YouTube (NULL = lê do nome do arquivo). */
static void toggle_in_queue(app_t *a, const char *path, const songinfo_t *info) {
  char line[512], msg[600];
  if (info) songinfo_display(info, line, sizeof line);
  else song_display_for_path(path, line, sizeof line);
  if (in_queue(a, path)) {
    queue_remove_path(&a->queue, QUEUE_HOST_NAME, path);
    snprintf(msg, sizeof msg, TR("Removed from queue: %s", "Tirada da fila: %s"), line);
    show_toast(a, msg);
    return;
  }
  queue_result_t r = queue_add_full(&a->queue, QUEUE_HOST_NAME, "", path, info);
  if (r == QUEUE_OK) snprintf(msg, sizeof msg, TR("In queue: %s", "Na fila: %s"), line);
  else snprintf(msg, sizeof msg, "%s", r == QUEUE_ERR_SINGER_FULL ? TR("The PS5 queue is full.", "A fila do PS5 está cheia.") : TR("Couldn't add to the queue.", "Não consegui adicionar à fila."));
  show_toast(a, msg);
}

static void song_line(const qsong_t *song, char *out, size_t size) {
  songinfo_display(&song->info, out, size);
}

static void start_countdown(app_t *a) {
  a->state = ST_COUNTDOWN;
  a->countdown_end = SDL_GetTicks() + COUNTDOWN_SEC * 1000;
  a->show_on = 1;
}

/* ▢ Iniciar show (busca e pastas). */
static void start_show(app_t *a) {
  if (queue_current(&a->queue, NULL)) start_countdown(a);
  else show_toast(a, TR("The queue is empty: add songs first.", "A fila está vazia: adicione músicas primeiro."));
}

/* Depois de uma música (ou de pular uma vez): próxima ou tela ociosa. */
static void after_turn(app_t *a) {
  if (queue_current(&a->queue, NULL)) start_countdown(a);
  else a->state = ST_IDLE;
}

/* ---------- Fila (OPTIONS) ---------- */

static void open_queue(app_t *a) {
  if (a->state == ST_QUEUE) return;
  a->queue_back = a->state;
  if (a->state == ST_COUNTDOWN) a->countdown_left = (Uint32)(a->countdown_end - SDL_GetTicks());
  queue_view_open(&a->queue_view);
  a->state = ST_QUEUE;
}

static void close_queue(app_t *a) {
  a->state = a->queue_back;
  if (a->state == ST_COUNTDOWN) {
    if (!queue_current(&a->queue, NULL)) a->state = ST_IDLE; /* tiraram tudo */
    else a->countdown_end = SDL_GetTicks() + a->countdown_left;
  }
}

static qv_action_t to_queue_action(action_t act) {
  switch (act) {
    case ACT_UP: return QV_UP;
    case ACT_DOWN: return QV_DOWN;
    case ACT_LEFT: return QV_LEFT;
    case ACT_RIGHT: return QV_RIGHT;
    case ACT_CROSS: return QV_GRAB;
    case ACT_TRIANGLE: return QV_REMOVE;
    case ACT_CIRCLE:
    case ACT_OPTIONS: return QV_CLOSE;
    default: return QV_NONE;
  }
}

/* A FILA por cima da música (playback_overlay_t). */
static void overlay_open(void *ctx) {
  app_t *a = ctx;
  reset_nav(a);
  queue_view_open(&a->queue_view);
}

static void overlay_draw(void *ctx) {
  app_t *a = ctx;
  queue_view_draw(&a->ui, &a->queue, &a->queue_view, a->playing_id);
}

static int overlay_event(void *ctx, const SDL_Event *e) {
  app_t *a = ctx;
  action_t act = translate_event(a, e);
  return queue_view_act(&a->queue, &a->queue_view, to_queue_action(act), a->playing_id);
}

static int overlay_animating(void *ctx) {
  return queue_view_has_toast(&((app_t *)ctx)->queue_view);
}

/* ---------- Celulares (servidor) ---------- */

/* IP do PS5 na rede local (sem mandar nada: só pergunta a rota). */
static void local_ip(char *out, size_t size) {
  snprintf(out, size, "0.0.0.0");
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return;
  struct sockaddr_in to = {0};
  to.sin_family = AF_INET;
  to.sin_port = htons(80);
  inet_pton(AF_INET, "1.1.1.1", &to.sin_addr);
  struct sockaddr_in me = {0};
  socklen_t len = sizeof me;
  if (connect(fd, (struct sockaddr *)&to, sizeof to) == 0 && getsockname(fd, (struct sockaddr *)&me, &len) == 0)
    inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)size);
  close(fd);
}

static void wake_main(void *ctx) {
  app_t *a = ctx;
  SDL_Event e;
  SDL_zero(e);
  e.type = a->wake_event;
  SDL_PushEvent(&e);
}

static int is_mode_playing(const app_t *a) { return a->playing_id != 0; }

static int is_mode_countdown(const app_t *a) {
  return a->state == ST_COUNTDOWN || (a->state == ST_QUEUE && a->queue_back == ST_COUNTDOWN);
}

static int countdown_secs(const app_t *a) {
  Sint32 left = is_mode_countdown(a) ? (Sint32)(a->state == ST_QUEUE ? (Sint32)a->countdown_left
                                                                       : (Sint32)(a->countdown_end - SDL_GetTicks()))
                                     : 0;
  return left <= 0 ? 0 : (left + 999) / 1000;
}

static void append(char *out, const char *text) {
  strncat(out, text, STATE_MAX - strlen(out) - 1);
}

static void append_song(char *out, const qsong_t *song, int semitones) {
  char buf[64];
  snprintf(buf, sizeof buf, "{\"id\":%d,\"semitones\":%d,\"artist\":", song->id, semitones);
  append(out, buf);
  json_append_string(out, STATE_MAX, song->info.artist);
  append(out, ",\"title\":");
  json_append_string(out, STATE_MAX, song->info.title);
  append(out, ",\"code\":");
  json_append_string(out, STATE_MAX, song->info.code);
  append(out, "}");
}

/* Retrato do estado para os celulares: modo da TV, música tocando e a
 * rotação a partir de quem está na vez (cantores com o hash do celular). */
static void build_state(app_t *a, char *out) {
  const char *mode = is_mode_playing(a) ? "playing" : is_mode_countdown(a) ? "countdown" : "idle";
  snprintf(out, STATE_MAX, "{\"v\":%u,\"lang\":\"%s\",\"mode\":\"%s\",\"countdown\":%d,\"url\":",
           a->queue.version, i18n_code(), mode, countdown_secs(a));
  json_append_string(out, STATE_MAX, a->url);

  singer_t *who;
  qsong_t *now = queue_current(&a->queue, &who);
  append(out, ",\"now\":");
  if (is_mode_playing(a) && now && now->id == a->playing_id) {
    char tag[9];
    server_client_tag(who->client, tag);
    append(out, "{\"singer\":");
    json_append_string(out, STATE_MAX, who->name);
    append(out, ",\"c\":\"");
    append(out, who->client[0] ? tag : "");
    append(out, "\",\"song\":");
    append_song(out, now, a->playing_semitones);
    append(out, "}");
  } else {
    append(out, "null");
  }

  append(out, ",\"rotation\":[");
  int start = a->queue.current < 0 ? 0 : a->queue.current;
  for (int k = 0; k < a->queue.count; k++) {
    const singer_t *s = &a->queue.singers[(start + k) % a->queue.count];
    char tag[9];
    server_client_tag(s->client, tag);
    append(out, k ? ",{\"name\":" : "{\"name\":");
    json_append_string(out, STATE_MAX, s->name);
    append(out, ",\"c\":\"");
    append(out, s->client[0] ? tag : "");
    append(out, "\",\"songs\":[");
    for (int j = 0; j < s->count; j++) {
      if (j) append(out, ",");
      append_song(out, &s->songs[j], s->songs[j].semitones);
    }
    append(out, "]}");
  }
  append(out, "]}");
}

/* Publica o retrato só quando algo que ele mostra mudou. */
static void publish_state(app_t *a) {
  if (!a->server) return;
  char key[96];
  snprintf(key, sizeof key, "%u|%d|%d|%d|%d|%d|%d", a->queue.version, is_mode_playing(a), is_mode_countdown(a),
           a->playing_id, a->playing_semitones, countdown_secs(a), (int)g_lang);
  if (strcmp(key, a->state_key) == 0) return;
  snprintf(a->state_key, sizeof a->state_key, "%s", key);
  build_state(a, a->state_json);
  server_set_state(a->server, a->state_json);
}

/* Só arquivos de karaokê dos HDs, da pasta interna ou da pasta de teste. */
static int path_allowed(const char *path) {
  size_t len = strlen(path);
  if (strstr(path, "..") || len < 5) return 0;
  if (strcasecmp(path + len - 4, ".zip") != 0 && strcasecmp(path + len - 4, ".mp4") != 0) return 0;
  return strncmp(path, "/mnt/usb", 8) == 0 || strncmp(path, "/mnt/ext", 8) == 0 ||
         strncmp(path, "/data/playkaraoke/songs/", 24) == 0 ||
         strncmp(path, "/data/playkaraoke/test/", 23) == 0;
}

static qsong_t *find_song(app_t *a, int id, singer_t **owner) {
  for (int i = 0; i < a->queue.count; i++)
    for (int j = 0; j < a->queue.singers[i].count; j++)
      if (a->queue.singers[i].songs[j].id == id) {
        *owner = &a->queue.singers[i];
        return &a->queue.singers[i].songs[j];
      }
  return NULL;
}

static void cmd_reply(server_cmd_t *cmd, int status, const char *error) {
  cmd->status = status;
  if (!error) {
    snprintf(cmd->body, sizeof cmd->body, "{\"ok\":true}");
  } else {
    snprintf(cmd->body, sizeof cmd->body, "{\"error\":");
    json_append_string(cmd->body, sizeof cmd->body, error);
    strncat(cmd->body, "}", sizeof cmd->body - strlen(cmd->body) - 1);
  }
}

static void run_cmd(app_t *a, server_cmd_t *cmd, playback_remote_t *remote) {
  singer_t *owner = NULL;
  switch (cmd->type) {
    case CMD_ADD: {
      char name[64];
      snprintf(name, sizeof name, "%s", cmd->name);
      while (name[0] == ' ') memmove(name, name + 1, strlen(name));
      for (size_t n = strlen(name); n && name[n - 1] == ' ';) name[--n] = '\0';
      if (!name[0]) { cmd_reply(cmd, 400, TR("Please enter your name.", "Digite o seu nome.")); break; }
      if (strcasecmp(name, QUEUE_HOST_NAME) == 0) { cmd_reply(cmd, 409, TR("That name is reserved: choose another one.", "Esse nome é reservado: escolha outro.")); break; }
      if (!path_allowed(cmd->path) || access(cmd->path, R_OK) != 0) {
        cmd_reply(cmd, 400, TR("That song is no longer on the drive.", "Essa música não está mais no HD."));
        break;
      }
      queue_result_t r = queue_add_full(&a->queue, name, cmd->client, cmd->path, NULL);
      if (r == QUEUE_ERR_SINGER_FULL) { cmd_reply(cmd, 409, TR("You already have 5 songs in the queue.", "Você já tem 5 músicas na fila.")); break; }
      if (r == QUEUE_ERR_NAME_TAKEN) { cmd_reply(cmd, 409, TR("That name is already taken by someone else.", "Esse nome já está sendo usado por outra pessoa.")); break; }
      if (r != QUEUE_OK) { cmd_reply(cmd, 500, TR("Couldn't request the song.", "Não consegui pedir a música.")); break; }
      cmd_reply(cmd, 200, NULL);
      char line[512], msg[640];
      song_display_for_path(cmd->path, line, sizeof line);
      snprintf(msg, sizeof msg, TR("%s requested: %s", "%s pediu: %s"), name, line);
      show_toast(a, msg);
      /* Show em andamento e a TV parada: começa a contagem sozinha. */
      if (a->show_on && a->state == ST_IDLE && !is_mode_playing(a)) start_countdown(a);
      break;
    }
    case CMD_CANCEL:
      if (cmd->song_id == a->playing_id) { cmd_reply(cmd, 409, TR("That song is playing: use Finish.", "Essa música está tocando: use Terminar.")); break; }
      if (queue_cancel(&a->queue, cmd->song_id, cmd->client) != QUEUE_OK) { cmd_reply(cmd, 403, TR("That song isn't yours.", "Essa música não é sua.")); break; }
      cmd_reply(cmd, 200, NULL);
      break;
    case CMD_PITCH: {
      qsong_t *song = find_song(a, cmd->song_id, &owner);
      if (!song || strcmp(owner->client, cmd->client) != 0) { cmd_reply(cmd, 403, TR("That song isn't yours.", "Essa música não é sua.")); break; }
      int st = cmd->semitones < -12 ? -12 : cmd->semitones > 12 ? 12 : cmd->semitones;
      song->semitones = st;
      a->queue.version++;
      if (song->id == a->playing_id && remote) {
        remote->set_pitch = 1;
        remote->semitones = st;
      }
      cmd_reply(cmd, 200, NULL);
      break;
    }
    case CMD_SHOT: /* só existe na versão de desenvolvimento */
      if (IMG_SavePNG(SDL_GetWindowSurface(a->win), SCREENSHOT_FILE) == 0) cmd_reply(cmd, 200, NULL);
      else cmd_reply(cmd, 500, IMG_GetError());
      break;
    case CMD_FINISH: {
      qsong_t *song = find_song(a, a->playing_id, &owner);
      if (!song || strcmp(owner->client, cmd->client) != 0) { cmd_reply(cmd, 403, TR("It's not your turn.", "Não é a sua vez.")); break; }
      if (remote) remote->finish = 1;
      cmd_reply(cmd, 200, NULL);
      break;
    }
  }
}

/* Executa os pedidos dos celulares e publica o estado. `remote` só existe
 * durante a música (tom e terminar ao vivo). */
static void service_phones(app_t *a, playback_remote_t *remote) {
  if (!a->server) return;
  if (server_pending(a->server)) {
    for (server_cmd_t *cmd = server_take(a->server), *next; cmd; cmd = next) {
      next = cmd->next;
      run_cmd(a, cmd, remote);
      server_done(a->server, cmd);
    }
  }
  publish_state(a);
}

static void overlay_tick(void *ctx, playback_remote_t *remote) {
  service_phones(ctx, remote);
}

static void draw_qr(app_t *a, int x, int y, int size);

/* Durante a música: QR code pequeno e fixo no canto inferior direito (PLK 2027). */
static void overlay_decorate(void *ctx) {
  app_t *a = ctx;
  ui_t *ui = &a->ui;
  const int w = 196, h = 244, x = UI_W - 24 - w, y = UI_H - 24 - h;
  ui_box(ui, x, y, w, h, 18, UI_POP, 235);
  roundedRectangleRGBA(ui->ren, (Sint16)x, (Sint16)y, (Sint16)(x + w - 1), (Sint16)(y + h - 1), 18,
                       (UI_BORDER >> 16) & 0xFF, (UI_BORDER >> 8) & 0xFF, UI_BORDER & 0xFF, 255);
  SDL_RenderFlush(ui->ren);
  draw_qr(a, x + 20, y + 16, 156);
  ui_text(ui, FONT_TINY, TR("Scan to add", "Leia para pedir"), x + w / 2, y + 180, UI_TEXT, ALIGN_CENTER, w - 12);
  ui_text(ui, FONT_TINY, TR("songs", "músicas"), x + w / 2, y + 204, UI_ACCENT, ALIGN_CENTER, w - 12);
}

/* ---------- Biblioteca e busca ---------- */

/* HDs montados e a pasta de teste (mesma lista da raiz das pastas). */
static void start_library_scan(app_t *a) {
  browser_t roots;
  browser_init(&roots);
  browser_open_root(&roots);
  const char *paths[16];
  int n = 0;
  for (int i = 0; i < roots.count && n < 16; i++) paths[n++] = roots.entries[i].path;
  if (library_scan_async(&a->library, paths, n, INDEX_FILE) == 0)
    printf("[app] atualizando a biblioteca (%d pastas)\n", n);
  browser_free(&roots);
}

static void run_search(app_t *a) {
  a->result_count = a->result_total = 0;
  a->result_count = library_search(&a->library, a->query, a->filter, a->results, LIBRARY_MAX_RESULTS,
                                   &a->result_total);
  a->result_sel = a->result_scroll = 0;
  a->result_library_count = library_count(&a->library);
  printf("[app] busca \"%s\" (filtro %d): %d de %d\n", a->query, a->filter, a->result_count, a->result_total);
}

static void open_keyboard(app_t *a) {
  a->ime_active = 1;
  a->ime_seen = 0;
  a->ime_text[0] = '\0';
  SDL_StartTextInput();
}

static void close_keyboard(app_t *a) {
  a->ime_active = 0;
  SDL_StopTextInput();
}

static void open_search(app_t *a) {
  if (a->state != ST_SEARCH && a->state != ST_BROWSER) a->back_state = a->state == ST_COUNTDOWN ? ST_IDLE : a->state;
  a->state = ST_SEARCH;
  run_search(a);
}

static void open_browser(app_t *a) {
  if (a->state != ST_SEARCH && a->state != ST_BROWSER) a->back_state = a->state == ST_COUNTDOWN ? ST_IDLE : a->state;
  if (browser_is_root(&a->browser)) browser_open_root(&a->browser); /* HDs podem ter mudado */
  a->state = ST_BROWSER;
}

/* ---------- Desenho ---------- */

static void draw_header(app_t *a, const char *title, const char *subtitle) {
  ui_t *ui = &a->ui;
  ui_clear(ui, UI_BG);
  ui_gradient_rect(ui, 0, 0, UI_W, 5);
  if (title && title[0]) ui_text(ui, FONT_HEADING, title, 80, 52, UI_TEXT, ALIGN_LEFT, 1200);
  if (subtitle && subtitle[0]) ui_text(ui, FONT_SMALL, subtitle, 80, 118, UI_TEXT_2, ALIGN_LEFT, 1400);

  char status[64];
  int total = queue_total_songs(&a->queue);
  if (total == 0) snprintf(status, sizeof status, TR("Queue empty", "Fila vazia"));
  else snprintf(status, sizeof status, TR("Queue: %d", "Fila: %d"), total);
  ui_badge(ui, UI_W - 80, 58, status, total ? UI_PANEL_HI : UI_PANEL, total ? UI_ACCENT : UI_TEXT_2, 1);
}

static void draw_toast(app_t *a) {
  if (!toast_visible(a)) return;
  ui_t *ui = &a->ui;
  int w = ui_text_width(ui, FONT_BODY, a->toast) + 80;
  if (w > UI_W - 160) w = UI_W - 160;
  int x = (UI_W - w) / 2, y = UI_H - 200;
  ui_box(ui, x, y, w, 72, 20, UI_PANEL_HI, 245);
  ui_text(ui, FONT_BODY, a->toast, UI_W / 2, y + 14, UI_TEXT, ALIGN_CENTER, w - 60);
}

/* QR code com o endereço do PS5 (quadrado de lado `size`, fundo branco). */
static void draw_qr(app_t *a, int x, int y, int size) {
  ui_t *ui = &a->ui;
  ui_box(ui, x, y, size, size, size / 16, 0xFFFFFF, 255);
  if (!a->qr_ok) return;
  int n = qrcodegen_getSize(a->qr);
  int module = size / (n + 6); /* margem de 3 módulos em volta */
  int ox = x + (size - module * n) / 2, oy = y + (size - module * n) / 2;
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++)
      if (qrcodegen_getModule(a->qr, c, r)) ui_rect(ui, ox + c * module, oy + r * module, module, module, 0x000000);
}

static void draw_idle(app_t *a) {
  ui_t *ui = &a->ui;
  ui_clear(ui, UI_BG);
  ui_gradient_rect(ui, 0, 0, UI_W, 5);

  /* Topo: L1 + bandeira BR (PT-BR) e R1 + bandeira EUA (EN), logo ao centro. */
  int lx_flag = 80;
  lx_flag += ui_hint(ui, lx_flag, 48, BTN_L1, "") + 8;
  if (g_lang == LANG_PT) ui_gradient_box(ui, lx_flag - 5, 43, 64, 46, 8);
  ui_flag(ui, 1, lx_flag, 48, 36);
  lx_flag += 78;
  lx_flag += ui_hint(ui, lx_flag, 48, BTN_R1, "") + 8;
  if (g_lang == LANG_EN) ui_gradient_box(ui, lx_flag - 5, 43, 64, 46, 8);
  ui_flag(ui, 0, lx_flag, 48, 36);
  char lib_status[96];
  if (a->library.scanning)
    snprintf(lib_status, sizeof lib_status, TR("Scanning... %d", "Buscando... %d"), a->library.scan_found);
  else
    snprintf(lib_status, sizeof lib_status, TR("Library: %d songs", "Biblioteca: %d músicas"), library_count(&a->library));
  ui_badge(ui, UI_W - 80, 54, lib_status, UI_PANEL, UI_TEXT_2, 1);

  if (!ui_logo(ui, UI_W / 2, 72)) ui_text(ui, FONT_TITLE, "PlayKaraoke", UI_W / 2, 130, UI_TEXT, ALIGN_CENTER, 0);

  /* Dois cartões equilibrados (PLK 2027): Esquerda = Show, Direita = Celular/QR. */
  const int card_y = 370, card_h = 560;
  const int lx = 80, lw = 1040;
  ui_card(ui, lx, card_y, lw, card_h, 24);

  singer_t *who;
  qsong_t *next = queue_current(&a->queue, &who);
  if (next) {
    char song[512];
    song_line(next, song, sizeof song);
    ui_badge(ui, lx + 48, card_y + 44, TR("UP NEXT", "A SEGUIR"), UI_PANEL_HI, UI_ACCENT, 0);
    ui_text(ui, FONT_TITLE, who->name, lx + 48, card_y + 104, UI_TEXT, ALIGN_LEFT, lw - 96);
    ui_text(ui, FONT_BODY_B, song, lx + 48, card_y + 206, UI_TEXT_2, ALIGN_LEFT, lw - 96);
    char total[64];
    int n = queue_total_songs(&a->queue);
    snprintf(total, sizeof total, n == 1 ? TR("1 song in queue", "1 música na fila") : TR("%d songs in queue", "%d músicas na fila"), n);
    ui_text(ui, FONT_SMALL, total, lx + 48, card_y + 276, UI_TEXT_3, ALIGN_LEFT, 0);
    if (a->show_on) ui_badge(ui, lx + 48, card_y + 324, TR("SHOW IN PROGRESS", "SHOW EM ANDAMENTO"), UI_FIELD, UI_OK, 0);

    /* Botão primário único em degradê. */
    ui_gradient_box(ui, lx + 48, card_y + card_h - 116, 440, 72, 16);
    ui_hint_bright(ui, lx + 78, card_y + card_h - 98, BTN_CROSS, TR("Start show", "Começar show"));
  } else {
    ui_badge(ui, lx + 48, card_y + 44, TR("LOCAL KARAOKE", "KARAOKÊ LOCAL"), UI_PANEL_HI, UI_ACCENT, 0);
    ui_text(ui, FONT_TITLE, TR("Ready to sing?", "Vamos cantar?"), lx + 48, card_y + 110, UI_TEXT, ALIGN_LEFT, lw - 96);
    ui_text(ui, FONT_BODY, TR("Guests scan the QR code to browse and request songs.", "Os convidados leem o QR code para pedir músicas."), lx + 48, card_y + 218, UI_TEXT_2, ALIGN_LEFT, lw - 96);
    ui_text(ui, FONT_BODY, TR("Or pick songs right here with the DualSense.", "Ou escolha as músicas aqui mesmo pelo DualSense."), lx + 48, card_y + 272, UI_TEXT_2, ALIGN_LEFT, lw - 96);

    ui_gradient_box(ui, lx + 48, card_y + card_h - 116, 440, 72, 16);
    ui_hint_bright(ui, lx + 78, card_y + card_h - 98, BTN_TRIANGLE, TR("Search songs", "Buscar música"));
  }

  /* Direita: cartão do celular com o QR code. */
  const int rx = 1160, rw = 680;
  ui_card(ui, rx, card_y, rw, card_h, 24);
  ui_text(ui, FONT_SMALL, TR("REQUEST FROM YOUR PHONE", "PEÇA PELO CELULAR"), rx + rw / 2, card_y + 36, UI_ACCENT, ALIGN_CENTER, 0);
  draw_qr(a, rx + (rw - 320) / 2, card_y + 84, 320);
  ui_text(ui, FONT_BODY_B, a->url, rx + rw / 2, card_y + 432, UI_ACCENT, ALIGN_CENTER, rw - 64);
  ui_text(ui, FONT_SMALL, TR("Connect to the same Wi-Fi as the PS5", "Conecte no mesmo Wi-Fi do PS5"), rx + rw / 2, card_y + 484, UI_TEXT_2, ALIGN_CENTER, rw - 64);

  if (next) {
    static const ui_button_t b[] = {BTN_CROSS, BTN_TRIANGLE, BTN_SQUARE, BTN_OPTIONS, BTN_CIRCLE};
    const char *const l[] = {TR("Start", "Começar"), TR("Search songs", "Buscar música"), TR("Folders", "Pastas"), TR("Queue", "Fila"), TR("Quit", "Sair")};
    ui_footer(ui, b, l, 5);
  } else {
    static const ui_button_t b[] = {BTN_TRIANGLE, BTN_SQUARE, BTN_CIRCLE};
    const char *const l[] = {TR("Search songs", "Buscar música"), TR("Folders", "Pastas"), TR("Quit", "Sair")};
    ui_footer(ui, b, l, 3);
  }
}

static void draw_folder_icon(ui_t *ui, int x, int y, int selected) {
  Uint32 c = selected ? UI_ACCENT : UI_TEXT_2;
  roundedRectangleRGBA(ui->ren, (Sint16)x, (Sint16)(y + 6), (Sint16)(x + 34), (Sint16)(y + 32), 4,
                       (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
  roundedBoxRGBA(ui->ren, (Sint16)x, (Sint16)(y + 2), (Sint16)(x + 15), (Sint16)(y + 8), 2,
                 (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
  SDL_RenderFlush(ui->ren);
}

/* Caixinha à esquerda: marcada = a música está na fila. */
static void draw_checkbox(ui_t *ui, int x, int y, int checked) {
  if (!checked) {
    ui_box(ui, x, y, 32, 32, 8, UI_BORDER, 255);
    ui_box(ui, x + 2, y + 2, 28, 28, 6, UI_FIELD, 255);
    return;
  }
  ui_gradient_box(ui, x, y, 32, 32, 8);
  thickLineRGBA(ui->ren, x + 7, y + 17, x + 13, y + 23, 3, 255, 255, 255, 255);
  thickLineRGBA(ui->ren, x + 13, y + 23, x + 25, y + 9, 3, 255, 255, 255, 255);
  SDL_RenderFlush(ui->ren);
}

static void draw_selection(ui_t *ui, int y) {
  ui_box(ui, 80, y, UI_W - 160, ROW_H - 6, 12, UI_PANEL_HI, 255);
  ui_gradient_rect(ui, 80, y + 10, 5, ROW_H - 26);
}

/* Uma linha de música: caixinha (na fila?), "Artista – Título", código e
 * formato à direita. */
static void draw_song_row(app_t *a, int y, const char *path, int selected) {
  ui_t *ui = &a->ui;
  if (selected) draw_selection(ui, y);
  draw_checkbox(ui, 108, y + 12, in_queue(a, path));
  const char *slash = strrchr(path, '/');
  songinfo_t info;
  songinfo_parse(slash ? slash + 1 : path, &info);
  char line[600];
  songinfo_display(&info, line, sizeof line);
  size_t len = strlen(path);
  int mp4 = len > 4 && strcasecmp(path + len - 4, ".mp4") == 0;
  int right = UI_W - 108;
  right -= ui_badge(ui, right, y + 11, mp4 ? "MP4" : "CDG", UI_FIELD, mp4 ? UI_BRAND_2 : UI_ACCENT, 1) + 18;
  if (info.code[0]) right -= ui_text(ui, FONT_SMALL, info.code, right, y + 14, UI_TEXT_3, ALIGN_RIGHT, 0) + 20;
  ui_text(ui, selected ? FONT_BODY_B : FONT_BODY, line, 162, y + 8, UI_TEXT, ALIGN_LEFT, right - 162);
}

/* Caminho amigável para o breadcrumb do navegador de pastas. */
static void friendly_path(const char *path, char *out, size_t size) {
  if (strncmp(path, SONGS_DIR, strlen(SONGS_DIR)) == 0) {
    snprintf(out, size, "%s%s", TR("PS5 Internal", "Armazenamento interno"), path + strlen(SONGS_DIR));
  } else if (strncmp(path, "/mnt/usb", 8) == 0 && path[8] >= '0' && path[8] <= '9') {
    snprintf(out, size, "%s %c%s", TR("USB Drive", "HD USB"), path[8], path + 9);
  } else if (strncmp(path, "/mnt/ext0", 9) == 0) {
    snprintf(out, size, "%s%s", TR("Extended Storage", "Armazenamento estendido"), path + 9);
  } else {
    snprintf(out, size, "%s", path);
  }
}

static void draw_browser(app_t *a) {
  ui_t *ui = &a->ui;
  browser_t *b = &a->browser;
  char crumb[1024];
  if (browser_is_root(b)) snprintf(crumb, sizeof crumb, "%s", TR("Choose where your songs are", "Escolha onde estão as músicas"));
  else friendly_path(b->path, crumb, sizeof crumb);
  draw_header(a, TR("Folders", "Pastas"), crumb);

  ui_card(ui, 64, LIST_Y - 16, UI_W - 128, ROWS * ROW_H + 26, 20);

  if (b->sel < b->scroll) b->scroll = b->sel;
  if (b->sel >= b->scroll + ROWS) b->scroll = b->sel - ROWS + 1;

  if (b->count == 0)
    ui_text(ui, FONT_BODY, TR("No songs in this folder.", "Nenhuma música nesta pasta."), UI_W / 2, 480, UI_TEXT_3, ALIGN_CENTER, 0);

  for (int r = 0; r < ROWS && b->scroll + r < b->count; r++) {
    int i = b->scroll + r;
    const bentry_t *e = &b->entries[i];
    int y = LIST_Y + r * ROW_H;
    if (e->is_dir) {
      if (i == b->sel) draw_selection(ui, y);
      draw_folder_icon(ui, 108, y + 10, i == b->sel);
      ui_text(ui, FONT_BODY_B, e->label, 162, y + 8, UI_TEXT, ALIGN_LEFT, UI_W - 320);
      ui_text(ui, FONT_BODY, "\xE2\x80\xBA", UI_W - 112, y + 8, i == b->sel ? UI_ACCENT : UI_TEXT_3, ALIGN_RIGHT, 0);
    } else {
      draw_song_row(a, y, e->path, i == b->sel);
    }
  }

  if (b->count > ROWS) {
    int track_h = ROWS * ROW_H - 6;
    int thumb_h = track_h * ROWS / b->count;
    if (thumb_h < 40) thumb_h = 40;
    int thumb_y = LIST_Y + (track_h - thumb_h) * b->scroll / (b->count - ROWS);
    ui_box(ui, UI_W - 44, LIST_Y, 8, track_h, 4, UI_BORDER, 255);
    ui_box(ui, UI_W - 44, thumb_y, 8, thumb_h, 4, UI_BRAND, 255);
  }

  const bentry_t *sel = browser_selected(b);
  const char *cross = !sel ? TR("Open", "Abrir") : sel->is_dir ? TR("Open", "Abrir") : in_queue(a, sel->path) ? TR("Remove", "Tirar da fila") : TR("Add", "Adicionar");
  const ui_button_t bt[] = {BTN_CROSS, BTN_SQUARE, BTN_OPTIONS, BTN_CIRCLE};
  const char *const l[] = {cross, TR("Start show", "Iniciar show"), TR("Queue", "Fila"), TR("Back", "Voltar")};
  ui_footer(ui, bt, l, 4);
  draw_toast(a);
}

static void draw_search(app_t *a) {
  ui_t *ui = &a->ui;
  char status[128];
  int total = library_count(&a->library);
  if (a->library.scanning)
    snprintf(status, sizeof status, TR("Updating the library... %d songs found", "Atualizando a biblioteca... %d músicas encontradas"), a->library.scan_found);
  else
    snprintf(status, sizeof status, TR("Library: %d songs", "Biblioteca: %d músicas"), total);
  draw_header(a, TR("Search songs", "Buscar música"), status);

  /* Campo de busca + pílulas segmentadas de formato (R1: All | CDG | MP4). */
  ui_card(ui, 64, 168, UI_W - 128, 92, 18);
  int fx = UI_W - 96;
  static const char *const flabels[3] = {"MP4", "CDG", NULL};
  for (int k = 0; k < 3; k++) {
    library_filter_t fk = (library_filter_t)(2 - k);
    const char *lbl = flabels[k] ? flabels[k] : TR("All", "Todas");
    int active = a->filter == fk;
    fx -= ui_badge(ui, fx, 197, lbl, active ? UI_BRAND : UI_FIELD, active ? 0xFFFFFF : UI_TEXT_2, 1) + 10;
  }
  fx -= ui_hint_width(ui, BTN_R1, "") + 8;
  ui_hint(ui, fx, 194, BTN_R1, "");
  int box_right = fx - 28;
  if (a->ime_active)
    ui_text(ui, FONT_BODY, TR("Type on the PS5 keyboard...", "Digite no teclado do PS5..."), 104, 192, UI_ACCENT, ALIGN_LEFT, 0);
  else if (a->query[0])
    ui_text(ui, FONT_BODY_B, a->query, 104, 192, UI_TEXT, ALIGN_LEFT, box_right - 104);
  else
    ui_text(ui, FONT_BODY, TR("Artist, song or code", "Artista, música ou código"), 104, 192, UI_TEXT_3, ALIGN_LEFT, 0);

  if (a->result_sel < a->result_scroll) a->result_scroll = a->result_sel;
  if (a->result_sel >= a->result_scroll + SEARCH_ROWS) a->result_scroll = a->result_sel - SEARCH_ROWS + 1;

  ui_card(ui, 64, SEARCH_LIST_Y - 14, UI_W - 128, SEARCH_ROWS * ROW_H + 22, 20);

  if (a->result_count == 0) {
    char msg[400];
    if (a->query[0]) snprintf(msg, sizeof msg, TR("Nothing found for \xE2\x80\x9C%s\xE2\x80\x9D.", "Nada encontrado para \xE2\x80\x9C%s\xE2\x80\x9D."), a->query);
    else snprintf(msg, sizeof msg, "%s", TR("No songs found.", "Nenhuma música encontrada."));
    ui_text(ui, FONT_BODY, msg, UI_W / 2, 480, UI_TEXT_2, ALIGN_CENTER, 1600);
    if (total == 0 && !a->library.scanning)
      ui_text(ui, FONT_SMALL, TR("The library is empty: connect a USB drive or add songs to /data/playkaraoke/songs.", "A biblioteca está vazia: conecte um HD USB ou adicione músicas em /data/playkaraoke/songs."), UI_W / 2, 550,
              UI_TEXT_3, ALIGN_CENTER, 1600);
  }
  for (int r = 0; r < SEARCH_ROWS && a->result_scroll + r < a->result_count; r++) {
    int i = a->result_scroll + r;
    int y = SEARCH_LIST_Y + r * ROW_H;
    draw_song_row(a, y, a->results[i], i == a->result_sel);
  }
  if (a->result_total > a->result_count) {
    char more[96];
    snprintf(more, sizeof more, TR("Showing %d of %d: refine your search.", "Mostrando %d de %d: refine a busca."), a->result_count, a->result_total);
    ui_text(ui, FONT_SMALL, more, UI_W / 2, UI_H - 138, UI_TEXT_3, ALIGN_CENTER, 0);
  }

  if (a->result_count) {
    const char *cross = in_queue(a, a->results[a->result_sel]) ? TR("Remove", "Tirar da fila") : TR("Add", "Adicionar");
    const ui_button_t b[] = {BTN_CROSS, BTN_SQUARE, BTN_TRIANGLE, BTN_OPTIONS, BTN_CIRCLE};
    const char *const l[] = {cross, TR("Start show", "Iniciar show"), TR("New search", "Nova busca"), TR("Queue", "Fila"), TR("Back", "Voltar")};
    ui_footer(ui, b, l, 5);
  } else {
    const ui_button_t b[] = {BTN_TRIANGLE, BTN_SQUARE, BTN_OPTIONS, BTN_CIRCLE};
    const char *const l[] = {TR("Type", "Digitar"), TR("Start show", "Iniciar show"), TR("Queue", "Fila"), TR("Back", "Voltar")};
    ui_footer(ui, b, l, 4);
  }
  draw_toast(a);
}

static void draw_countdown(app_t *a) {
  ui_t *ui = &a->ui;
  draw_header(a, "", NULL);
  singer_t *who;
  qsong_t *song = queue_current(&a->queue, &who);
  if (!song) return;

  /* Cartão de palco (esquerda): cantor, música, tom, barra de progresso e botão primário. */
  const int lx = 80, ly = 140, lw = 1140, lh = 790;
  ui_card(ui, lx, ly, lw, lh, 24);
  ui_badge(ui, lx + 48, ly + 44, TR("UP NEXT", "A SEGUIR"), UI_PANEL_HI, UI_ACCENT, 0);
  if (song->semitones != 0) {
    char kbuf[24];
    snprintf(kbuf, sizeof kbuf, TR("Key %+d", "Tom %+d"), song->semitones);
    ui_badge(ui, lx + lw - 48, ly + 44, kbuf, UI_FIELD, UI_BRAND_2, 1);
  }

  char line[512];
  song_line(song, line, sizeof line);
  ui_text(ui, FONT_TITLE, who->name, lx + 48, ly + 114, UI_TEXT, ALIGN_LEFT, lw - 96);
  ui_text(ui, FONT_HEADING, line, lx + 48, ly + 220, UI_TEXT_2, ALIGN_LEFT, lw - 96);
  if (song->info.code[0]) ui_text(ui, FONT_SMALL, song->info.code, lx + 48, ly + 290, UI_TEXT_3, ALIGN_LEFT, 0);

  Sint32 left_ms = (Sint32)(a->countdown_end - SDL_GetTicks());
  if (left_ms < 0) left_ms = 0;
  int secs = (left_ms + 999) / 1000;
  char ctxt[64];
  snprintf(ctxt, sizeof ctxt, TR("Starting in %ds", "Começando em %ds"), secs);
  ui_text(ui, FONT_HEADING, ctxt, lx + 48, ly + 420, UI_ACCENT, ALIGN_LEFT, 0);

  /* Barra de progresso em degradê (10s -> 0s). */
  const int bx = lx + 48, by = ly + 498, bw = lw - 96, bh = 22;
  ui_box(ui, bx, by, bw, bh, 11, UI_FIELD, 255);
  int fill = bw * (COUNTDOWN_SEC * 1000 - left_ms) / (COUNTDOWN_SEC * 1000);
  if (fill < 22) fill = 22;
  if (fill > bw) fill = bw;
  ui_gradient_box(ui, bx, by, fill, bh, 11);

  /* Botão primário único em degradê. */
  ui_gradient_box(ui, lx + 48, ly + lh - 120, 420, 74, 16);
  ui_hint_bright(ui, lx + 78, ly + lh - 101, BTN_CROSS, TR("Start now", "Começar agora"));

  /* Coluna direita: QR compacto no topo + "AFTER THAT" embaixo. */
  const int rx = 1256, rw = 584;
  ui_card(ui, rx, ly, rw, 350, 24);
  ui_text(ui, FONT_SMALL, TR("SCAN TO ADD SONGS", "LEIA PARA ADICIONAR"), rx + rw / 2, ly + 24, UI_ACCENT, ALIGN_CENTER, 0);
  draw_qr(a, rx + (rw - 210) / 2, ly + 66, 210);
  ui_text(ui, FONT_SMALL, a->url, rx + rw / 2, ly + 294, UI_TEXT_2, ALIGN_CENTER, rw - 40);

  const int ay = ly + 374, ah = lh - 374;
  ui_card(ui, rx, ay, rw, ah, 24);
  ui_text(ui, FONT_SMALL, TR("AFTER THAT", "DEPOIS"), rx + 36, ay + 28, UI_TEXT_3, ALIGN_LEFT, 0);
  singer_t *ns[3];
  qsong_t *nq[3];
  int n = queue_upcoming(&a->queue, ns, nq, 3);
  if (n == 0) {
    ui_text(ui, FONT_SMALL, TR("No more songs in queue.", "Nenhuma outra música na fila."), rx + 36, ay + 96, UI_TEXT_3, ALIGN_LEFT, rw - 72);
  }
  for (int i = 0; i < n; i++) {
    int ry = ay + 84 + i * 100;
    char s[512], num[16];
    song_line(nq[i], s, sizeof s);
    snprintf(num, sizeof num, "#%d  %s", i + 1, ns[i]->name);
    ui_text(ui, FONT_BODY_B, num, rx + 36, ry, UI_ACCENT, ALIGN_LEFT, rw - 72);
    ui_text(ui, FONT_SMALL, s, rx + 36, ry + 42, UI_TEXT_2, ALIGN_LEFT, rw - 72);
  }

  static const ui_button_t b[] = {BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_OPTIONS};
  const char *const l[] = {TR("Start now", "Começar agora"), TR("Skip turn", "Pular a vez"), TR("Search songs", "Buscar música"), TR("Queue", "Fila")};
  ui_footer(ui, b, l, 4);
  draw_toast(a);
}

static void draw_confirm_exit(app_t *a) {
  ui_t *ui = &a->ui;
  draw_idle(a);
  ui_box(ui, 0, 0, UI_W, UI_H, 0, 0x000000, 160);
  const int w = 900, h = 300, x = (UI_W - w) / 2, y = (UI_H - h) / 2;
  ui_box(ui, x, y, w, h, 28, UI_PANEL, 255);
  ui_text(ui, FONT_HEADING, TR("Quit PlayKaraoke?", "Sair do PlayKaraoke?"), UI_W / 2, y + 50, UI_TEXT, ALIGN_CENTER, 0);
  ui_text(ui, FONT_BODY, TR("This session's queue and singers will be cleared.", "A fila e os cantores desta sessão serão apagados."), UI_W / 2, y + 120,
          UI_TEXT_2, ALIGN_CENTER, w - 80);
  ui_hint(ui, x + 230, y + 210, BTN_CROSS, TR("Quit", "Sair"));
  ui_hint(ui, x + 520, y + 210, BTN_CIRCLE, TR("Cancel", "Cancelar"));
}

static void draw(app_t *a) {
  switch (a->state) {
    case ST_IDLE: draw_idle(a); draw_toast(a); break;
    case ST_SEARCH: draw_search(a); break;
    case ST_BROWSER: draw_browser(a); break;
    case ST_COUNTDOWN: draw_countdown(a); break;
    case ST_QUEUE: queue_view_draw(&a->ui, &a->queue, &a->queue_view, 0); break;
    case ST_CONFIRM_EXIT: draw_confirm_exit(a); break;
    default: return;
  }
  ui_present(&a->ui);
}

/* ---------- Ações por tela ---------- */

static void on_idle(app_t *a, action_t act) {
  if (act == ACT_L1) i18n_set(LANG_PT);
  else if (act == ACT_R1) i18n_set(LANG_EN);
  else if (act == ACT_TRIANGLE) open_search(a);
  else if (act == ACT_SQUARE) open_browser(a);
  else if (act == ACT_CROSS && queue_current(&a->queue, NULL)) start_countdown(a);
  else if (act == ACT_CIRCLE) a->state = ST_CONFIRM_EXIT;
}

static void on_browser(app_t *a, action_t act) {
  browser_t *b = &a->browser;
  const bentry_t *sel = browser_selected(b);
  switch (act) {
    case ACT_UP: browser_move(b, -1); break;
    case ACT_DOWN: browser_move(b, 1); break;
    case ACT_LEFT: browser_move(b, -ROWS); break;
    case ACT_RIGHT: browser_move(b, ROWS); break;
    case ACT_CROSS:
      if (!sel) break;
      if (sel->is_dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s", sel->path);
        if (browser_open(b, path) != 0) show_toast(a, TR("Couldn't open that folder.", "Não consegui abrir essa pasta."));
      } else {
        toggle_in_queue(a, sel->path, NULL);
      }
      break;
    case ACT_SQUARE: start_show(a); break;
    case ACT_CIRCLE:
      if (browser_up(b) != 0) a->state = a->back_state;
      break;
    default:
      break;
  }
}

static void on_search(app_t *a, action_t act) {
  const char *sel = a->result_count ? a->results[a->result_sel] : NULL;
  switch (act) {
    case ACT_UP:
      if (a->result_count) a->result_sel = (a->result_sel + a->result_count - 1) % a->result_count;
      break;
    case ACT_DOWN:
      if (a->result_count) a->result_sel = (a->result_sel + 1) % a->result_count;
      break;
    case ACT_LEFT:
      a->result_sel = a->result_sel > SEARCH_ROWS ? a->result_sel - SEARCH_ROWS : 0;
      break;
    case ACT_RIGHT:
      if (a->result_count)
        a->result_sel = a->result_sel + SEARCH_ROWS < a->result_count ? a->result_sel + SEARCH_ROWS
                                                                        : a->result_count - 1;
      break;
    case ACT_CROSS:
      if (sel) toggle_in_queue(a, sel, NULL);
      break;
    case ACT_SQUARE: start_show(a); break;
    case ACT_TRIANGLE: open_keyboard(a); break;
    case ACT_R1:
      a->filter = (a->filter + 1) % 3;
      run_search(a);
      break;
    case ACT_TEXT_ENTER:
      close_keyboard(a);
      snprintf(a->query, sizeof a->query, "%s", a->ime_text);
      run_search(a);
      break;
    case ACT_CIRCLE: a->state = a->back_state; break;
    default: break;
  }
}

static void on_countdown(app_t *a, action_t act) {
  if (act == ACT_CROSS) {
    a->state = ST_PLAYING;
  } else if (act == ACT_CIRCLE) {
    singer_t *who;
    if (queue_current(&a->queue, &who)) {
      char msg[128];
      snprintf(msg, sizeof msg, TR("Skipped %s's turn.", "Vez de %s pulada."), who->name);
      show_toast(a, msg);
    }
    queue_skip_turn(&a->queue);
    after_turn(a);
  } else if (act == ACT_TRIANGLE) {
    open_search(a);
  }
}

static void play_current(app_t *a) {
  singer_t *who;
  qsong_t *song = queue_current(&a->queue, &who);
  if (!song) {
    a->state = ST_IDLE;
    return;
  }
  char path[1024], error[256];
  snprintf(path, sizeof path, "%s", song->path);
  int id = song->id;
  printf("[app] vez de %s: %s\n", who->name, path);

  static const playback_overlay_t overlay_fns = {NULL,          overlay_open,      overlay_draw, overlay_event,
                                                 overlay_animating, overlay_tick, overlay_decorate};
  playback_overlay_t overlay = overlay_fns;
  overlay.ctx = a;

  reset_nav(a);
  a->playing_id = id;
  a->playing_semitones = song->semitones;
  publish_state(a);
  char display[512];
  songinfo_display(&song->info, display, sizeof display);
  playback_result_t r = playback_play(a->win, &a->osd, &overlay, path, display, &a->playing_semitones,
                                      &a->volume, error, sizeof error);
  a->playing_id = 0;
  a->playing_semitones = 0;
  reset_nav(a);

  queue_complete(&a->queue, id);
  if (r == PLAYBACK_FAILED) show_toast(a, error);
  after_turn(a);
}

/* ---------- Principal ---------- */

int sceUserServiceInitialize(void *params);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

int main(void) {
  printf("[app] iniciando\n");
  i18n_load();
  /* Serviço de usuário do PS5: sem ele, o navegador que abre os vídeos do
   * YouTube fica sem usuário (sceUserServiceGetInitialUser 0x80960006). */
  int us = sceUserServiceInitialize(NULL);
  if (us) printf("[app] sceUserServiceInitialize: 0x%08x\n", (unsigned)us);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("[app] SDL_Init falhou: %s\n", SDL_GetError());
    return 1;
  }

  /* O SDL do PacBrew não tem renderer acelerado na janela: tudo é desenhado
   * por software na superfície da janela e enviado com SDL_UpdateWindowSurface. */
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");

  static app_t app;
  app_t *a = &app;
  a->win = SDL_CreateWindow("PlayKaraoke", 0, 0, UI_W, UI_H, 0);
  if (!a->win || ui_init(&a->ui, a->win) != 0) {
    printf("[app] janela/interface falhou: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  mkdir(CACHE_DIR, 0755);
  mkdir(SONGS_DIR, 0755);
  osd_init(&a->osd, &a->ui);
  queue_init(&a->queue);
  browser_init(&a->browser);
  library_init(&a->library);
  a->results = calloc(LIBRARY_MAX_RESULTS, sizeof *a->results);
  if (!a->results) return 1;
  /* Índice salvo vale na hora; a varredura atualiza em segundo plano. */
  printf("[app] biblioteca: %d musicas no indice salvo\n", library_load(&a->library, INDEX_FILE));
  start_library_scan(a);
  a->volume = VOLUME_DEFAULT;
  a->state = a->back_state = ST_IDLE;

  /* Celulares: servidor, endereço e QR code. */
  a->wake_event = SDL_RegisterEvents(1);
  a->state_json = malloc(STATE_MAX);
  char ip[32];
  local_ip(ip, sizeof ip);
  snprintf(a->url, sizeof a->url, "http://%s:%d", ip, SERVER_PORT);
  uint8_t qr_temp[qrcodegen_BUFFER_LEN_MAX];
  a->qr_ok = qrcodegen_encodeText(a->url, qr_temp, a->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                  qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
  if (a->state_json)
    a->server = server_start(SERVER_PORT, &a->library, www_index_html, www_index_html_size, wake_main, a);
  printf("[app] celulares: %s (%s)\n", a->url, a->server ? "servidor ok" : "servidor falhou");

  Uint32 last_draw = 0;
  int dirty = 1;
  while (a->state != ST_EXIT) {
    if (a->state == ST_PLAYING) {
      play_current(a);
      dirty = 1;
      continue;
    }

    if (dirty) {
      draw(a);
      last_draw = SDL_GetTicks();
      dirty = 0;
    }

    action_t act = poll_action(a, 100);
    if (act == ACT_QUIT) break;
    if (act != ACT_NONE) dirty = 1;

    unsigned version = a->queue.version;
    state_t before = a->state;
    service_phones(a, NULL);
    if (a->queue.version != version || a->state != before) dirty = 1;

    /* Teclado do PS5 fechado sem OK = cancelado. */
    if (a->ime_active) {
      if (SDL_IsScreenKeyboardShown(a->win)) a->ime_seen = 1;
      else if (a->ime_seen && act != ACT_TEXT_ENTER) {
        close_keyboard(a);
        dirty = 1;
      }
    }

    /* OPTIONS abre a fila de qualquer tela. */
    if (act == ACT_OPTIONS && a->state != ST_QUEUE && a->state != ST_CONFIRM_EXIT) {
      open_queue(a);
      continue;
    }

    switch (a->state) {
      case ST_IDLE: on_idle(a, act); break;
      case ST_SEARCH:
        on_search(a, act);
        /* A biblioteca terminou de atualizar: refaz a busca atual. */
        if (!a->library.scanning && library_count(&a->library) != a->result_library_count) {
          run_search(a);
          dirty = 1;
        }
        break;
      case ST_BROWSER: on_browser(a, act); break;
      case ST_QUEUE:
        if (queue_view_act(&a->queue, &a->queue_view, to_queue_action(act), 0)) close_queue(a);
        break;
      case ST_COUNTDOWN:
        on_countdown(a, act);
        if (a->state == ST_COUNTDOWN && (Sint32)(SDL_GetTicks() - a->countdown_end) >= 0)
          a->state = ST_PLAYING;
        break;
      case ST_CONFIRM_EXIT:
        if (act == ACT_CROSS) a->state = ST_EXIT;
        else if (act == ACT_CIRCLE) a->state = ST_IDLE;
        break;
      default:
        break;
    }

    /* Contagem, avisos e a atualização da biblioteca mudam sozinhos:
     * redesenha a cada 250 ms enquanto houver algo assim na tela. */
    int animating = a->state == ST_COUNTDOWN || toast_visible(a) || a->toast[0] ||
                    (a->state == ST_SEARCH && a->library.scanning) ||
                    (a->state == ST_QUEUE && queue_view_has_toast(&a->queue_view));
    if (animating && SDL_GetTicks() - last_draw >= 250) {
      dirty = 1;
      if (!toast_visible(a)) a->toast[0] = '\0';
    }
  }

  printf("[app] fim\n");
  fflush(stdout);
  library_free(&a->library);
  ui_free(&a->ui);
  SDL_DestroyWindow(a->win);
  SDL_Quit();
  sceSystemServiceLoadExec("exit", NULL);
  _exit(0);
}
