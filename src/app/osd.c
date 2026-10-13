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
  const int w = 640, h = 150, x = (UI_W - w) / 2, y = 64;
  ui_box(ui, x, y, w, h, 24, 0x000000, 200);
  ui_text(ui, FONT_HEADING, title, x + 40, y + 22, UI_TEXT, ALIGN_LEFT, 0);
  ui_text(ui, FONT_HEADING, value, x + w - 40, y + 22, UI_TEXT, ALIGN_RIGHT, 0);
  SDL_Rect bar = {x + 40, y + 104, w - 80, 16};
  return bar;
}

static void draw_fill_bar(osd_t *o, SDL_Rect bar, double fraction) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  ui_box(o->ui, bar.x, bar.y, bar.w, bar.h, 8, 0xFFFFFF, 60);
  int fill = (int)(bar.w * fraction);
  if (fill > bar.h) ui_box(o->ui, bar.x, bar.y, fill, bar.h, 8, UI_BRAND, 255);
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
  ui_box(ui, bar.x, bar.y, bar.w, bar.h, 8, 0xFFFFFF, 60);
  int center = bar.x + bar.w / 2;
  int pos = center + bar.w / 2 * o->semitones / 12;
  if (pos != center) {
    int left = pos < center ? pos : center;
    ui_rect(ui, left, bar.y, (pos < center ? center - pos : pos - center), bar.h, UI_BRAND);
  }
  ui_rect(ui, center - 2, bar.y - 6, 4, bar.h + 12, UI_TEXT);
}

static void draw_paused(osd_t *o) {
  ui_t *ui = o->ui;
  const int w = 640, h = 220, x = (UI_W - w) / 2, y = (UI_H - h) / 2;
  ui_box(ui, x, y, w, h, 28, 0x000000, 200);
  ui_text(ui, FONT_TITLE, TR("Paused", "Pausado"), UI_W / 2, y + 36, UI_TEXT, ALIGN_CENTER, 0);
  int hint_w = 40 + 12 + ui_text_width(ui, FONT_SMALL, TR("Resume", "Continuar"));
  ui_hint(ui, UI_W / 2 - hint_w / 2, y + 150, BTN_CROSS, TR("Resume", "Continuar"));
}

/* Barra de comandos no rodapé, por cima da música. */
static void draw_controls(osd_t *o) {
  ui_t *ui = o->ui;
  static const ui_button_t b[] = {BTN_CROSS, BTN_CIRCLE, BTN_OPTIONS, BTN_DPAD, BTN_L1R1, BTN_RSTICK};
  const char *const l[] = {TR("Pause", "Pausar"), TR("Next song", "Próxima música"), TR("Queue", "Fila"), "Volume", TR("Key", "Tom"), TR("Back/forward 10 s", "Voltar/avançar 10 s")};
  const int n = 6, gap = 44, h = 84;
  int widths[6], total = 0;
  for (int i = 0; i < n; i++) {
    widths[i] = ui_hint_width(ui, b[i], l[i]);
    total += widths[i] + (i ? gap : 0);
  }
  /* Centralizada no espaço à esquerda do QR code do canto (240 px). */
  int x = (UI_W - 240 - total) / 2 - 40, y = UI_H - h - 40;
  ui_box(ui, x, y, total + 80, h, 26, 0x000000, 200);
  x += 40;
  for (int i = 0; i < n; i++) {
    ui_hint(ui, x, y + 24, b[i], l[i]);
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
