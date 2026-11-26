/* PlayKaraoke PS5 — avisos rápidos por cima da música. Ver osd.h. */

#include "osd.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

#define TOAST_SHOW_MS 2000
#define CONTROLS_SHOW_MS 5000

void osd_init(osd_t *o, ui_t *ui) {
  memset(o, 0, sizeof *o);
  o->ui = ui;
}

static void show_toast(osd_t *o, osd_toast_t toast) {
  o->toast = toast;
  o->toast_until = SDL_GetTicks() + TOAST_SHOW_MS;
  o->dirty = 1;
}

void osd_show_volume(osd_t *o, int pct) {
  o->volume_pct = pct;
  show_toast(o, OSD_TOAST_VOLUME);
}

void osd_show_seek(osd_t *o, double pos, double duration) {
  o->seek_pos = pos;
  o->seek_duration = duration;
  show_toast(o, OSD_TOAST_SEEK);
}

void osd_show_pitch(osd_t *o, int semitones) {
  o->semitones = semitones;
  show_toast(o, OSD_TOAST_PITCH);
}

void osd_set_paused(osd_t *o, int paused) {
  if (o->paused != paused) o->dirty = 1;
  o->paused = paused;
}

static int toast_visible(const osd_t *o) {
  return o->toast != OSD_TOAST_NONE && (Sint32)(o->toast_until - SDL_GetTicks()) > 0;
}

static int controls_visible(const osd_t *o) {
  return o->controls_until && (Sint32)(o->controls_until - SDL_GetTicks()) > 0;
}

void osd_show_controls(osd_t *o) {
  o->controls_until = SDL_GetTicks() + CONTROLS_SHOW_MS;
  o->dirty = 1;
}

int osd_visible(const osd_t *o) {
  return o->paused || toast_visible(o) || controls_visible(o);
}

int osd_changed(osd_t *o) {
  int visible = osd_visible(o);
  if (visible != o->was_visible) o->dirty = 1;
  o->was_visible = visible;
  int changed = o->dirty;
  o->dirty = 0;
  return changed;
}

/* Caixa no alto da tela: título à esquerda, valor à direita, barra embaixo
 * (o desenho da barra fica com quem chama). Devolve o retângulo da barra. */
static SDL_Rect draw_panel(osd_t *o, const char *title, const char *value) {
  ui_t *ui = o->ui;
  const int w = 640, h = 148, x = (UI_W - w) / 2, y = 56;
  ui_box(ui, x, y, w, h, 22, UI_POP, 235);
  ui_text(ui, FONT_HEADING, title, x + 36, y + 22, UI_TEXT, ALIGN_LEFT, 0);
  ui_text(ui, FONT_HEADING, value, x + w - 36, y + 22, UI_ACCENT, ALIGN_RIGHT, 0);
  SDL_Rect bar = {x + 36, y + 102, w - 72, 16};
  return bar;
}

static void draw_fill_bar(osd_t *o, SDL_Rect bar, double fraction) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  ui_box(o->ui, bar.x, bar.y, bar.w, bar.h, 8, UI_FIELD, 255);
  int fill = (int)(bar.w * fraction);
  if (fill > bar.h) ui_gradient_box(o->ui, bar.x, bar.y, fill, bar.h, 8);
}

static void format_time(char *out, size_t size, double sec) {
  int s = sec < 0 ? 0 : (int)sec;
  snprintf(out, size, "%d:%02d", s / 60, s % 60);
}

static void draw_volume(osd_t *o) {
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", o->volume_pct);
  draw_fill_bar(o, draw_panel(o, "Volume", pct), o->volume_pct / 100.0);
}

static void draw_seek(osd_t *o) {
  char pos[16], dur[16], value[40];
  format_time(pos, sizeof pos, o->seek_pos);
  format_time(dur, sizeof dur, o->seek_duration);
  snprintf(value, sizeof value, "%s / %s", pos, dur);
  double fraction = o->seek_duration > 0 ? o->seek_pos / o->seek_duration : 0;
  draw_fill_bar(o, draw_panel(o, TR("Time", "Tempo"), value), fraction);
}

/* Tom: barra que parte do centro (tom original) para a esquerda (mais
 * grave) ou para a direita (mais agudo), com marcas a cada semitom. */
static void draw_pitch(osd_t *o) {
  char value[16];
  if (o->semitones == 0) snprintf(value, sizeof value, TR("Original", "Original"));
  else snprintf(value, sizeof value, "%+d", o->semitones);
  SDL_Rect bar = draw_panel(o, TR("Key", "Tom"), value);

  ui_t *ui = o->ui;
  ui_box(ui, bar.x, bar.y, bar.w, bar.h, 8, UI_FIELD, 255);
  int center = bar.x + bar.w / 2;
  int pos = center + bar.w / 2 * o->semitones / 12;
  if (pos != center) {
    int left = pos < center ? pos : center;
    ui_gradient_rect(ui, left, bar.y, (pos < center ? center - pos : pos - center), bar.h);
  }
  ui_rect(ui, center - 2, bar.y - 6, 4, bar.h + 12, UI_TEXT);
}

static void draw_paused(osd_t *o) {
  ui_t *ui = o->ui;
  const int w = 640, h = 230, x = (UI_W - w) / 2, y = (UI_H - h) / 2;
  ui_box(ui, x, y, w, h, 26, UI_POP, 238);
  ui_text(ui, FONT_TITLE, TR("Paused", "Pausado"), UI_W / 2, y + 36, UI_TEXT, ALIGN_CENTER, 0);
  int w1 = ui_hint_width(ui, BTN_CROSS, TR("Resume", "Continuar"));
  int w2 = ui_hint_width(ui, BTN_CIRCLE, TR("Next song", "Próxima"));
  int total = w1 + 48 + w2;
  int hx = UI_W / 2 - total / 2;
  ui_hint(ui, hx, y + 150, BTN_CROSS, TR("Resume", "Continuar"));
  ui_hint(ui, hx + w1 + 48, y + 150, BTN_CIRCLE, TR("Next song", "Próxima"));
}

/* Barra de comandos no rodapé, por cima da música. */
static void draw_controls(osd_t *o) {
  ui_t *ui = o->ui;
  static const ui_button_t b[] = {BTN_CROSS, BTN_CIRCLE, BTN_OPTIONS, BTN_DPAD, BTN_L1R1, BTN_RSTICK};
  const char *const l[] = {TR("Pause", "Pausar"), TR("Next", "Próxima"), TR("Queue", "Fila"), "Volume", TR("Key", "Tom"), TR("±10s", "±10s")};
  const int n = 6, gap = 40, h = 80;
  int widths[6], total = 0;
  for (int i = 0; i < n; i++) {
    widths[i] = ui_hint_width(ui, b[i], l[i]);
    total += widths[i] + (i ? gap : 0);
  }
  /* Centralizada no espaço à esquerda do QR code do canto (240 px), translúcida. */
  int x = (UI_W - 240 - total) / 2 - 36, y = UI_H - h - 36;
  if (x < 24) x = 24;
  ui_box(ui, x, y, total + 72, h, 22, UI_POP, 185);
  x += 36;
  for (int i = 0; i < n; i++) {
    ui_hint(ui, x, y + 22, b[i], l[i]);
    x += widths[i] + gap;
  }
}

void osd_draw(osd_t *o) {
  if (o->paused) draw_paused(o);
  if (controls_visible(o)) draw_controls(o);
  if (!toast_visible(o)) return;
  if (o->toast == OSD_TOAST_VOLUME) draw_volume(o);
  else if (o->toast == OSD_TOAST_SEEK) draw_seek(o);
  else if (o->toast == OSD_TOAST_PITCH) draw_pitch(o);
}
