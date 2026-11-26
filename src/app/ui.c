/* PlayKaraoke PS5 — base das telas. Ver ui.h. */

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include <SDL2_gfxPrimitives.h>
#include <SDL_image.h>

#include "assets.h"

static TTF_Font *open_font(const uint8_t *data, uint64_t size, int pt) {
  SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
  if (!rw) return NULL;
  TTF_Font *f = TTF_OpenFontRW(rw, 1, pt);
  if (!f) printf("[ui] TTF_OpenFontRW: %s\n", TTF_GetError());
  return f;
}

int ui_init(ui_t *ui, SDL_Window *win) {
  memset(ui, 0, sizeof *ui);
  ui->win = win;
  ui->screen = SDL_GetWindowSurface(win);
  if (!ui->screen) return -1;
  if (TTF_Init() != 0) {
    printf("[ui] TTF_Init: %s\n", TTF_GetError());
    return -1;
  }
  ui->ren = SDL_CreateSoftwareRenderer(ui->screen);

  ui->font[FONT_HUGE] = open_font(font_montserrat_bold, font_montserrat_bold_size, 160);
  ui->font[FONT_TITLE] = open_font(font_montserrat_bold, font_montserrat_bold_size, 72);
  ui->font[FONT_HEADING] = open_font(font_montserrat_bold, font_montserrat_bold_size, 44);
  ui->font[FONT_BODY] = open_font(font_inter_regular, font_inter_regular_size, 34);
  ui->font[FONT_BODY_B] = open_font(font_inter_semibold, font_inter_semibold_size, 34);
  ui->font[FONT_SMALL] = open_font(font_inter_regular, font_inter_regular_size, 26);
  ui->font[FONT_TINY] = open_font(font_inter_regular, font_inter_regular_size, 19);
  for (int i = 0; i < FONT_COUNT; i++)
    if (!ui->font[i]) return -1;

  SDL_RWops *rw = SDL_RWFromConstMem(logo_png, (int)logo_png_size);
  SDL_Surface *logo = rw ? IMG_LoadPNG_RW(rw) : NULL;
  if (rw) SDL_RWclose(rw);
  if (logo) {
    ui->logo = SDL_ConvertSurfaceFormat(logo, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(logo);
    if (ui->logo) SDL_SetSurfaceBlendMode(ui->logo, SDL_BLENDMODE_BLEND);
  }
  if (!ui->logo) printf("[ui] logo indisponivel: %s\n", IMG_GetError());

  SDL_RWops *irw = SDL_RWFromConstMem(icons_dualsense_png, (int)icons_dualsense_png_size);
  SDL_Surface *icons = irw ? IMG_LoadPNG_RW(irw) : NULL;
  if (irw) SDL_RWclose(irw);
  if (icons) {
    ui->icons = SDL_ConvertSurfaceFormat(icons, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(icons);
    if (ui->icons) SDL_SetSurfaceBlendMode(ui->icons, SDL_BLENDMODE_BLEND);
  }
  if (!ui->icons) printf("[ui] icones DualSense indisponiveis: %s\n", IMG_GetError());
  return ui->ren ? 0 : -1;
}

void ui_free(ui_t *ui) {
  if (ui->icons) SDL_FreeSurface(ui->icons);
  if (ui->logo) SDL_FreeSurface(ui->logo);
  for (int i = 0; i < FONT_COUNT; i++)
    if (ui->font[i]) TTF_CloseFont(ui->font[i]);
  if (ui->ren) SDL_DestroyRenderer(ui->ren);
  TTF_Quit();
  memset(ui, 0, sizeof *ui);
}

static Uint32 map(ui_t *ui, Uint32 rgb) {
  return SDL_MapRGB(ui->screen->format, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void ui_clear(ui_t *ui, Uint32 rgb) {
  SDL_FillRect(ui->screen, NULL, map(ui, rgb));
}

void ui_rect(ui_t *ui, int x, int y, int w, int h, Uint32 rgb) {
  SDL_Rect r = {x, y, w, h};
  SDL_FillRect(ui->screen, &r, map(ui, rgb));
}

void ui_box(ui_t *ui, int x, int y, int w, int h, int radius, Uint32 rgb, Uint8 alpha) {
  if (w <= 0 || h <= 0) return;
  if (radius > h / 2) radius = h / 2;
  if (radius > w / 2) radius = w / 2;
  roundedBoxRGBA(ui->ren, (Sint16)x, (Sint16)y, (Sint16)(x + w - 1), (Sint16)(y + h - 1), (Sint16)radius,
                 (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
  SDL_RenderFlush(ui->ren);
}

void ui_card(ui_t *ui, int x, int y, int w, int h, int radius) {
  ui_box(ui, x, y, w, h, radius, UI_PANEL, 255);
  roundedRectangleRGBA(ui->ren, (Sint16)x, (Sint16)y, (Sint16)(x + w - 1), (Sint16)(y + h - 1), (Sint16)radius,
                       (UI_BORDER >> 16) & 0xFF, (UI_BORDER >> 8) & 0xFF, UI_BORDER & 0xFF, 255);
  SDL_RenderFlush(ui->ren);
}

static Uint32 lerp_rgb(Uint32 a, Uint32 b, int num, int den) {
  if (den <= 0) return a;
  int r = ((int)((a >> 16) & 0xFF) * (den - num) + (int)((b >> 16) & 0xFF) * num) / den;
  int g = ((int)((a >> 8) & 0xFF) * (den - num) + (int)((b >> 8) & 0xFF) * num) / den;
  int bl = ((int)(a & 0xFF) * (den - num) + (int)(b & 0xFF) * num) / den;
  return (Uint32)((r << 16) | (g << 8) | bl);
}

void ui_gradient_rect(ui_t *ui, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  int step = w > 120 ? 4 : 2;
  for (int i = 0; i < w; i += step) {
    int sw = i + step > w ? w - i : step;
    Uint32 c = lerp_rgb(UI_GRAD_FROM, UI_GRAD_TO, i, w);
    SDL_Rect r = {x + i, y, sw, h};
    SDL_FillRect(ui->screen, &r, map(ui, c));
  }
}

void ui_gradient_box(ui_t *ui, int x, int y, int w, int h, int radius) {
  if (w <= 0 || h <= 0) return;
  if (radius > h / 2) radius = h / 2;
  if (radius > w / 2) radius = w / 2;
  /* Pontas arredondadas com as cores extremas + corpo em degradê. */
  if (radius > 0) {
    ui_box(ui, x, y, radius * 2 + 2, h, radius, UI_GRAD_FROM, 255);
    ui_box(ui, x + w - radius * 2 - 2, y, radius * 2 + 2, h, radius, UI_GRAD_TO, 255);
    if (w > radius * 2) ui_gradient_rect(ui, x + radius, y, w - radius * 2, h);
  } else {
    ui_gradient_rect(ui, x, y, w, h);
  }
}

int ui_badge(ui_t *ui, int x, int y, const char *text, Uint32 bg, Uint32 fg, int align_right) {
  int tw = ui_text_width(ui, FONT_TINY, text);
  int w = tw + 24, h = 34;
  int bx = align_right ? x - w : x;
  ui_box(ui, bx, y, w, h, h / 2, bg, 255);
  ui_text(ui, FONT_TINY, text, bx + w / 2, y + 5, fg, ALIGN_CENTER, 0);
  return w;
}

int ui_text_width(ui_t *ui, ui_font_t font, const char *text) {
  int w = 0, h = 0;
  if (!text[0] || TTF_SizeUTF8(ui->font[font], text, &w, &h) != 0) return 0;
  return w;
}

int ui_line_height(ui_t *ui, ui_font_t font) {
  return TTF_FontHeight(ui->font[font]);
}

/* Corta o texto (respeitando UTF-8) e põe "…" até caber em max_w. */
static void ellipsize(ui_t *ui, ui_font_t font, const char *text, int max_w, char *out, size_t size) {
  snprintf(out, size, "%s", text);
  if (ui_text_width(ui, font, out) <= max_w) return;
  size_t len = strlen(out);
  while (len > 0) {
    do len--; while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
    while (len > 0 && out[len - 1] == ' ') len--;
    if (len + 4 > size) continue;
    memcpy(out + len, "\xE2\x80\xA6", 4); /* "…" + '\0' */
    if (ui_text_width(ui, font, out) <= max_w) return;
  }
  snprintf(out, size, "\xE2\x80\xA6");
}

int ui_text(ui_t *ui, ui_font_t font, const char *text, int x, int y, Uint32 rgb,
            ui_align_t align, int max_w) {
  if (!text || !text[0]) return 0;
  char buf[1024];
  if (max_w > 0) ellipsize(ui, font, text, max_w, buf, sizeof buf);
  else snprintf(buf, sizeof buf, "%s", text);

  SDL_Color color = {(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255};
  SDL_Surface *s = TTF_RenderUTF8_Blended(ui->font[font], buf, color);
  if (!s) return 0;
  SDL_Rect dst = {x, y, s->w, s->h};
  if (align == ALIGN_CENTER) dst.x = x - s->w / 2;
  else if (align == ALIGN_RIGHT) dst.x = x - s->w;
  SDL_BlitSurface(s, NULL, ui->screen, &dst);
  int w = s->w;
  SDL_FreeSurface(s);
  return w;
}

#define ICON_SLOT_W 64
#define ICON_SLOT_H 48

static int button_width(ui_t *ui, ui_button_t button) {
  (void)ui;
  switch (button) {
    case BTN_L1R1: return 62;
    case BTN_RSTICK: return 56;
    case BTN_L1:
    case BTN_R1:
    case BTN_OPTIONS:
    case BTN_TOUCH: return 48;
    default: return 42;
  }
}

int ui_hint_width(ui_t *ui, ui_button_t button, const char *label) {
  return button_width(ui, button) + (label[0] ? 10 + ui_text_width(ui, FONT_SMALL, label) : 0);
}

/* Desenha o ícone do DualSense a partir do spritesheet (assets/icons/dualsense.png). */
static int draw_button(ui_t *ui, int x, int y, ui_button_t button) {
  int w = button_width(ui, button);
  if (ui->icons) {
    SDL_Rect src = {(int)button * ICON_SLOT_W + (ICON_SLOT_W - w) / 2, 0, w, ICON_SLOT_H};
    SDL_Rect dst = {x, y - 5, w, ICON_SLOT_H};
    SDL_BlitSurface(ui->icons, &src, ui->screen, &dst);
    return w;
  }
  /* Fallback caso o spritesheet falhe. */
  static const char *const names[] = {"X", "O", "/\\", "[]", "+", "L1", "R1", "L1/R1", "R", "OPT", "PAD"};
  ui_box(ui, x, y, w, 36, 10, UI_PANEL_HI, 255);
  ui_text(ui, FONT_TINY, names[button], x + w / 2, y + 6, UI_TEXT, ALIGN_CENTER, 0);
  return w;
}

int ui_hint(ui_t *ui, int x, int y, ui_button_t button, const char *label) {
  int w = draw_button(ui, x, y, button);
  if (label[0]) {
    w += 10;
    w += ui_text(ui, FONT_SMALL, label, x + w, y + 2, UI_TEXT_2, ALIGN_LEFT, 0);
  }
  return w;
}

int ui_hint_bright(ui_t *ui, int x, int y, ui_button_t button, const char *label) {
  int w = draw_button(ui, x, y, button);
  if (label[0]) {
    w += 10;
    w += ui_text(ui, FONT_BODY_B, label, x + w, y - 2, 0xFFFFFF, ALIGN_LEFT, 0);
  }
  return w;
}

void ui_footer(ui_t *ui, const ui_button_t *buttons, const char *const *labels, int count) {
  const int y = UI_H - 80;
  ui_rect(ui, 0, y - 24, UI_W, 1, UI_BORDER);
  int x = 80;
  for (int i = 0; i < count; i++) x += ui_hint(ui, x, y, buttons[i], labels[i]) + 44;
}

void ui_flag(ui_t *ui, int lang, int x, int y, int h) {
  SDL_Renderer *ren = ui->ren;
  int w = h * 3 / 2;
  if (lang == 0) {
    /* EUA: 13 listras, cantão azul com estrelas (pontos). */
    for (int i = 0; i < 13; i++) {
      int y0 = y + h * i / 13, y1 = y + h * (i + 1) / 13 - 1;
      if (i % 2 == 0) boxRGBA(ren, x, y0, x + w - 1, y1, 0xB2, 0x22, 0x34, 255);
      else boxRGBA(ren, x, y0, x + w - 1, y1, 0xFF, 0xFF, 0xFF, 255);
    }
    int cw = w * 2 / 5, ch = h * 7 / 13;
    boxRGBA(ren, x, y, x + cw - 1, y + ch - 1, 0x3C, 0x3B, 0x6E, 255);
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 5; c++)
        filledCircleRGBA(ren, x + cw * (2 * c + 1) / 10, y + ch * (2 * r + 1) / 8, h / 40 + 1, 255, 255, 255, 255);
  } else {
    /* Brasil: verde, losango amarelo e círculo azul. */
    boxRGBA(ren, x, y, x + w - 1, y + h - 1, 0x00, 0x9C, 0x3B, 255);
    Sint16 vx[4] = {(Sint16)(x + w / 2), (Sint16)(x + w - h / 10), (Sint16)(x + w / 2), (Sint16)(x + h / 10)};
    Sint16 vy[4] = {(Sint16)(y + h / 10), (Sint16)(y + h / 2), (Sint16)(y + h - h / 10), (Sint16)(y + h / 2)};
    filledPolygonRGBA(ren, vx, vy, 4, 0xFF, 0xDF, 0x00, 255);
    filledCircleRGBA(ren, x + w / 2, y + h / 2, h * 22 / 100, 0x00, 0x27, 0x76, 255);
  }
  SDL_RenderFlush(ren);
}

int ui_logo(ui_t *ui, int cx, int y) {
  if (!ui->logo) return 0;
  SDL_Rect dst = {cx - ui->logo->w / 2, y, ui->logo->w, ui->logo->h};
  SDL_BlitSurface(ui->logo, NULL, ui->screen, &dst);
  return ui->logo->h;
}

void ui_present(ui_t *ui) {
  SDL_UpdateWindowSurface(ui->win);
}
