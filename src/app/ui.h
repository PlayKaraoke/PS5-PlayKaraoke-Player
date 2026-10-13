/* PlayKaraoke PS5 — base das telas: fontes (Inter e Montserrat via
 * SDL2_ttf), texto com acento e reticências, caixas arredondadas. Tudo é
 * desenhado na superfície da janela (1920x1080); ui_present envia à TV. */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#define UI_W 1920
#define UI_H 1080

/* Cores (0xRRGGBB). */
#define UI_BG 0x0F0C17
#define UI_PANEL 0x1C1726
#define UI_PANEL_HI 0x2E2340
#define UI_TEXT 0xFFFFFF
#define UI_TEXT_2 0xB4ADC2
#define UI_TEXT_3 0x7D7590
#define UI_BRAND 0xB000E0
#define UI_BRAND_2 0x5A12F0

typedef enum {
  FONT_HUGE,    /* Montserrat Bold 160: número da contagem */
  FONT_TITLE,   /* Montserrat Bold 72 */
  FONT_HEADING, /* Montserrat Bold 44 */
  FONT_BODY,    /* Inter 34 */
  FONT_BODY_B,  /* Inter SemiBold 34 */
  FONT_SMALL,   /* Inter 26 */
  FONT_TINY,    /* Inter 19 */
  FONT_COUNT
} ui_font_t;

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } ui_align_t;

typedef struct ui {
  SDL_Window *win;
  SDL_Surface *screen;
  SDL_Renderer *ren; /* software, sobre a superfície (caixas do SDL2_gfx) */
  TTF_Font *font[FONT_COUNT];
  SDL_Surface *logo; /* logo branco da PlayKaraoke (pode faltar) */
} ui_t;

int ui_init(ui_t *ui, SDL_Window *win);
void ui_free(ui_t *ui);

void ui_clear(ui_t *ui, Uint32 rgb);
void ui_rect(ui_t *ui, int x, int y, int w, int h, Uint32 rgb);
/* Caixa arredondada; alpha 0-255. */
void ui_box(ui_t *ui, int x, int y, int w, int h, int radius, Uint32 rgb, Uint8 alpha);

/* Texto UTF-8 em (x, y = topo). Com max_w > 0, corta com "…" se não couber.
 * Devolve a largura desenhada. */
int ui_text(ui_t *ui, ui_font_t font, const char *text, int x, int y, Uint32 rgb,
            ui_align_t align, int max_w);
int ui_text_width(ui_t *ui, ui_font_t font, const char *text);
int ui_line_height(ui_t *ui, ui_font_t font);

/* Botões do DualSense para as dicas de rodapé. */
typedef enum { BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_SQUARE, BTN_OPTIONS, BTN_L1R1, BTN_L1, BTN_R1, BTN_RSTICK, BTN_DPAD } ui_button_t;

/* Desenha o botão e o texto ao lado (y = topo da linha de FONT_SMALL).
 * Devolve a largura total. */
int ui_hint(ui_t *ui, int x, int y, ui_button_t button, const char *label);

/* Largura do botão + texto, como ui_hint desenharia. */
int ui_hint_width(ui_t *ui, ui_button_t button, const char *label);

/* Faixa de dicas no rodapé: `count` pares botão/texto. */
void ui_footer(ui_t *ui, const ui_button_t *buttons, const char *const *labels, int count);

/* Bandeira do idioma (0 = EUA, 1 = Brasil), altura h, largura 1,5 x h. */
void ui_flag(ui_t *ui, int lang, int x, int y, int h);

/* Logo centralizado em cx, com o topo em y. Devolve a altura (0 se faltar). */
int ui_logo(ui_t *ui, int cx, int y);

void ui_present(ui_t *ui);
