/* PlayKaraoke PS5 — tela FILA. Ver queue_view.h. */

#include "queue_view.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

#define LIST_Y 190
#define ROW_H 58
#define ROWS ((UI_H - LIST_Y - 150) / ROW_H)
#define MAX_ROWS (QUEUE_MAX_SINGERS * (QUEUE_HOST_MAX_SONGS + 1))
#define TOAST_MS 2500

/* Linhas da lista: cabeçalho do cantor ou música. */
typedef struct row {
  int singer, song; /* song = -1 no cabeçalho */
} row_t;

static row_t g_rows[MAX_ROWS];

/* Monta as linhas na ordem da rotação, a partir do cantor da vez. */
static int build_rows(queue_t *q) {
  int n = 0;
  queue_current(q, NULL); /* acerta `current` pulando quem está sem música */
  if (q->count == 0) return 0;
  int start = q->current < 0 ? 0 : q->current;
  for (int k = 0; k < q->count; k++) {
    int s = (start + k) % q->count;
    g_rows[n++] = (row_t){s, -1};
    for (int j = 0; j < q->singers[s].count; j++) g_rows[n++] = (row_t){s, j};
  }
  return n;
}

static int is_song_row(int i) { return g_rows[i].song >= 0; }

/* Seleção sempre numa música (pula os cabeçalhos). */
static void fix_selection(queue_view_t *v, int n, int dir) {
  if (n == 0) {
    v->sel = 0;
    return;
  }
  if (v->sel >= n) v->sel = n - 1;
  if (v->sel < 0) v->sel = 0;
  for (int tries = 0; tries < n && !is_song_row(v->sel); tries++) {
    v->sel += dir >= 0 ? 1 : -1;
    if (v->sel >= n) v->sel = 0;
    if (v->sel < 0) v->sel = n - 1;
  }
}

void queue_view_open(queue_view_t *v) {
  memset(v, 0, sizeof *v);
}

int queue_view_has_toast(const queue_view_t *v) {
  return v->toast[0] && (Sint32)(v->toast_until - SDL_GetTicks()) > -300;
}

static void show_toast(queue_view_t *v, const char *msg) {
  snprintf(v->toast, sizeof v->toast, "%s", msg);
  v->toast_until = SDL_GetTicks() + TOAST_MS;
}

void queue_view_draw(ui_t *ui, queue_t *q, queue_view_t *v, int playing_id) {
  int n = build_rows(q);
  fix_selection(v, n, 1);

  ui_clear(ui, UI_BG);
  ui_rect(ui, 0, 0, UI_W, 6, UI_BRAND);
  ui_text(ui, FONT_HEADING, TR("Queue", "Fila"), 80, 56, UI_TEXT, ALIGN_LEFT, 0);
  int total = queue_total_songs(q);
  char status[64];
  snprintf(status, sizeof status, total == 1 ? TR("1 song", "1 música") : TR("%d songs", "%d músicas"), total);
  ui_text(ui, FONT_BODY, status, UI_W - 80, 62, UI_TEXT_2, ALIGN_RIGHT, 0);
  ui_text(ui, FONT_SMALL, TR("Rotation order: each singer sings one song per round.", "Ordem da rotação: cada cantor canta uma música por rodada."), 80, 124, UI_TEXT_3,
          ALIGN_LEFT, 0);

  if (total == 0)
    ui_text(ui, FONT_BODY, TR("The queue is empty.", "A fila está vazia."), UI_W / 2, 480, UI_TEXT_3, ALIGN_CENTER, 0);

  if (v->sel < v->scroll) v->scroll = v->sel;
  if (v->sel >= v->scroll + ROWS) v->scroll = v->sel - ROWS + 1;
  /* Mostra o cabeçalho do cantor quando a primeira música dele está no topo. */
  if (v->scroll > 0 && !is_song_row(v->scroll - 1) && v->sel - (v->scroll - 1) < ROWS) v->scroll--;

  for (int r = 0; r < ROWS && v->scroll + r < n; r++) {
    int i = v->scroll + r;
    int y = LIST_Y + r * ROW_H;
    singer_t *s = &q->singers[g_rows[i].singer];
    if (!is_song_row(i)) {
      char head[128];
      if (s->count == 0) snprintf(head, sizeof head, TR("%s  \xC2\xB7  no songs (will be skipped)", "%s  \xC2\xB7  sem música (será pulado)"), s->name);
      else snprintf(head, sizeof head, TR("%s  \xC2\xB7  %d song%s", "%s  \xC2\xB7  %d música%s"), s->name, s->count, s->count == 1 ? "" : "s");
      ui_text(ui, FONT_BODY_B, head, 80, y + 12, s->count ? UI_BRAND : UI_TEXT_3, ALIGN_LEFT, UI_W - 200);
      continue;
    }
    qsong_t *song = &s->songs[g_rows[i].song];
    if (i == v->sel && v->grabbed) {
      ui_box(ui, 64, y, UI_W - 128, ROW_H - 6, 16, UI_BRAND_2, 255);
    } else if (i == v->sel) {
      ui_box(ui, 64, y, UI_W - 128, ROW_H - 6, 16, UI_PANEL_HI, 255);
      ui_rect(ui, 64, y + 13, 6, ROW_H - 32, UI_BRAND);
    }
    char num[8], line[600];
    snprintf(num, sizeof num, "%d", g_rows[i].song + 1);
    ui_text(ui, FONT_SMALL, num, 130, y + 12, UI_TEXT_3, ALIGN_RIGHT, 0);
    songinfo_display(&song->info, line, sizeof line);
    int right = UI_W - 100;
    const char *tag = song->id == playing_id ? TR("PLAYING", "TOCANDO")
                      : (g_rows[i].singer == q->current && g_rows[i].song == 0 && !playing_id) ? TR("NEXT", "PRÓXIMA")
                                                                                              : NULL;
    if (tag) right -= ui_text(ui, FONT_SMALL, tag, right, y + 12, UI_BRAND, ALIGN_RIGHT, 0) + 24;
    if (song->semitones) {
      char st[16];
      snprintf(st, sizeof st, TR("key %+d", "tom %+d"), song->semitones);
      right -= ui_text(ui, FONT_SMALL, st, right, y + 12, UI_TEXT_2, ALIGN_RIGHT, 0) + 24;
    }
    ui_text(ui, FONT_BODY, line, 160, y + 6, UI_TEXT, ALIGN_LEFT, right - 160);
  }

  if (queue_view_has_toast(v) && (Sint32)(v->toast_until - SDL_GetTicks()) > 0) {
    int w = ui_text_width(ui, FONT_BODY, v->toast) + 80;
    int x = (UI_W - w) / 2, y = UI_H - 200;
    ui_box(ui, x, y, w, 72, 20, UI_PANEL_HI, 245);
    ui_text(ui, FONT_BODY, v->toast, UI_W / 2, y + 14, UI_TEXT, ALIGN_CENTER, w - 60);
  }

  if (v->grabbed) {
    static const ui_button_t b[] = {BTN_DPAD, BTN_CROSS};
    const char *const l[] = {TR("Move up/down", "Mover para cima/baixo"), TR("Drop here", "Soltar aqui")};
    ui_footer(ui, b, l, 2);
  } else if (total) {
    static const ui_button_t b[] = {BTN_DPAD, BTN_CROSS, BTN_TRIANGLE, BTN_CIRCLE};
    const char *const l[] = {TR("Select", "Escolher"), TR("Move", "Mudar de lugar"), TR("Remove song", "Remover música"), TR("Close", "Fechar")};
    ui_footer(ui, b, l, 4);
  } else {
    static const ui_button_t b[] = {BTN_CIRCLE};
    const char *const l[] = {TR("Close", "Fechar")};
    ui_footer(ui, b, l, 1);
  }
}

/* Música pega: ↑/↓ mudam a posição dela na lista do cantor. */
static void move_grabbed(queue_t *q, queue_view_t *v, int dir, int playing_id) {
  row_t r = g_rows[v->sel];
  /* Não passa por cima da música que está tocando (a primeira do cantor). */
  if (r.song + dir == 0 && q->singers[r.singer].songs[0].id == playing_id) return;
  int to = queue_move_song(q, r.singer, r.song, r.song + dir);
  v->sel += to - r.song; /* as linhas do mesmo cantor são seguidas */
}

int queue_view_act(queue_t *q, queue_view_t *v, qv_action_t act, int playing_id) {
  int n = build_rows(q);
  if (v->grabbed) {
    if (act == QV_UP) move_grabbed(q, v, -1, playing_id);
    else if (act == QV_DOWN) move_grabbed(q, v, 1, playing_id);
    else if (act == QV_GRAB || act == QV_CLOSE) v->grabbed = 0;
    return 0;
  }
  switch (act) {
    case QV_GRAB: {
      if (n == 0 || !is_song_row(v->sel)) break;
      row_t r = g_rows[v->sel];
      if (q->singers[r.singer].songs[r.song].id == playing_id) show_toast(v, TR("That song is playing now.", "Essa música está tocando agora."));
      else if (q->singers[r.singer].count < 2) show_toast(v, TR("This singer has only one song.", "Esse cantor só tem uma música."));
      else v->grabbed = 1;
      break;
    }
    case QV_UP: v->sel--; fix_selection(v, n, -1); break;
    case QV_DOWN: v->sel++; if (v->sel >= n) v->sel = 0; fix_selection(v, n, 1); break;
    case QV_LEFT: v->sel -= ROWS; fix_selection(v, n, -1); break;
    case QV_RIGHT: v->sel += ROWS; fix_selection(v, n, 1); break;
    case QV_REMOVE: {
      if (n == 0 || !is_song_row(v->sel)) break;
      row_t r = g_rows[v->sel];
      qsong_t *song = &q->singers[r.singer].songs[r.song];
      if (song->id == playing_id) {
        show_toast(v, TR("That song is playing now.", "Essa música está tocando agora."));
        break;
      }
      char line[600], msg[160];
      songinfo_display(&song->info, line, sizeof line);
      snprintf(msg, sizeof msg, TR("Removed: %s", "Removida: %s"), line);
      queue_remove_song(q, r.singer, r.song);
      show_toast(v, msg);
      fix_selection(v, build_rows(q), -1);
      break;
    }
    case QV_CLOSE: return 1;
    default: break;
  }
  return 0;
}
