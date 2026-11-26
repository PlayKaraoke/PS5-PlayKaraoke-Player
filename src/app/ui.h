/* PlayKaraoke PS5 — base das telas: fontes (Inter e Montserrat via
 * SDL2_ttf), texto com acento e reticências, caixas arredondadas. Tudo é
 * desenhado na superfície da janela (1920x1080); ui_present envia à TV. */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#define UI_W 1920
#define UI_H 1080

/* Cores PLK 2027 (0xRRGGBB). */
#define UI_BG        0x0D0D1A
#define UI_PANEL     0x141420
#define UI_PANEL_HI  0x1E1B2E
#define UI_FIELD     0x10101B
#define UI_POP       0x181826
#define UI_BORDER    0x2A2436
#define UI_TEXT      0xECEBF2
#define UI_TEXT_2    0xA7A5B7
#define UI_TEXT_3    0x77768C
#define UI_ACCENT    0xC2ABFF
#define UI_BRAND     0x8B5CF6
#define UI_BRAND_2   0xEC4899
#define UI_GRAD_FROM 0x8655F6
#define UI_GRAD_TO   0xE2177B
#define UI_OK        0x85DDBD
#define UI_WARN      0xECC287
#define UI_ERR       0xFF969E

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
  SDL_Surface *logo;  /* logo branco da PlayKaraoke (pode faltar) */
  SDL_Surface *icons; /* spritesheet DualSense 704x48 (11 slots de 64x48) */
} ui_t;

int ui_init(ui_t *ui, SDL_Window *win);
void ui_free(ui_t *ui);

void ui_clear(ui_t *ui, Uint32 rgb);
void ui_rect(ui_t *ui, int x, int y, int w, int h, Uint32 rgb);
/* Caixa arredondada; alpha 0-255. */
void ui_box(ui_t *ui, int x, int y, int w, int h, int radius, Uint32 rgb, Uint8 alpha);
/* Cartão PLK 2027: preenchimento UI_PANEL + borda 1px UI_BORDER. */
void ui_card(ui_t *ui, int x, int y, int w, int h, int radius);
/* Retângulo com degradê horizontal UI_GRAD_FROM -> UI_GRAD_TO. */
void ui_gradient_rect(ui_t *ui, int x, int y, int w, int h);
/* Caixa arredondada com degradê horizontal UI_GRAD_FROM -> UI_GRAD_TO. */
void ui_gradient_box(ui_t *ui, int x, int y, int w, int h, int radius);
/* Pílula/etiqueta pequena. Retorna a largura desenhada. */
int ui_badge(ui_t *ui, int x, int y, const char *text, Uint32 bg, Uint32 fg, int align_right);

/* Texto UTF-8 em (x, y = topo). Com max_w > 0, corta com "…" se não couber.
 * Devolve a largura desenhada. */
int ui_text(ui_t *ui, ui_font_t font, const char *text, int x, int y, Uint32 rgb,
            ui_align_t align, int max_w);
int ui_text_width(ui_t *ui, ui_font_t font, const char *text);
int ui_line_height(ui_t *ui, ui_font_t font);

/* Botões do DualSense para as dicas de rodapé. */
typedef enum {
  BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_SQUARE, BTN_DPAD,
  BTN_L1, BTN_R1, BTN_L1R1, BTN_RSTICK, BTN_OPTIONS, BTN_TOUCH
} ui_button_t;

/* Desenha o botão e o texto ao lado (y = topo da linha de FONT_SMALL).
 * Devolve a largura total. */
int ui_hint(ui_t *ui, int x, int y, ui_button_t button, const char *label);
int ui_hint_bright(ui_t *ui, int x, int y, ui_button_t button, const char *label);

/* Largura do botão + texto, como ui_hint desenharia. */
int ui_hint_width(ui_t *ui, ui_button_t button, const char *label);

/* Faixa de dicas no rodapé: `count` pares botão/texto. */
void ui_footer(ui_t *ui, const ui_button_t *buttons, const char *const *labels, int count);

/* Bandeira do idioma (0 = EUA, 1 = Brasil), altura h, largura 1,5 x h. */
void ui_flag(ui_t *ui, int lang, int x, int y, int h);

/* Logo centralizado em cx, com o topo em y. Devolve a altura (0 se faltar). */
int ui_logo(ui_t *ui, int cx, int y);

void ui_present(ui_t *ui);
