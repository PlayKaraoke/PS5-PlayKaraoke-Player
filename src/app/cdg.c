/* PlayKaraoke PS5 — decodificador CD+Graphics. Ver cdg.h. */

#include "cdg.h"

#include <string.h>

#define PACKET_SIZE 24
#define PACKETS_PER_SECOND 300
#define TILE_W 6
#define TILE_H 12
#define TILE_COLS (CDG_WIDTH / TILE_W)  /* 50 */
#define TILE_ROWS (CDG_HEIGHT / TILE_H) /* 18 */

#define CDG_COMMAND 0x09
#define CDG_MASK 0x3F
#define INS_MEMORY_PRESET 1
#define INS_BORDER_PRESET 2
#define INS_TILE_BLOCK 6
#define INS_SCROLL_PRESET 20
#define INS_SCROLL_COPY 24
#define INS_TRANSPARENT_COLOR 28
#define INS_LOAD_CLUT_LOW 30
#define INS_LOAD_CLUT_HIGH 31
#define INS_TILE_BLOCK_XOR 38

void cdg_reset(cdg_t *c) {
  memset(c->pixels, 0, sizeof c->pixels);
  memset(c->palette, 0, sizeof c->palette);
  c->border_color = 0;
  c->last_index = -1;
  c->changed = 1;
}

void cdg_init(cdg_t *c, const uint8_t *data, size_t size) {
  c->data = data;
  c->packet_count = size / PACKET_SIZE;
  cdg_reset(c);
}

static void memory_preset(cdg_t *c, const uint8_t *d) {
  memset(c->pixels, d[0] & 0x0F, sizeof c->pixels);
  c->changed = 1;
}

static void border_preset(cdg_t *c, const uint8_t *d) {
  c->border_color = d[0] & 0x0F;
  c->changed = 1;
}

/* Cor CDG: 4 bits por canal (12-bit RGB), expandidos para 8 bits. */
static void load_clut(cdg_t *c, const uint8_t *d, int start) {
  for (int i = 0; i < 8; i++) {
    uint8_t b0 = d[i * 2], b1 = d[i * 2 + 1];
    int r4 = (b0 >> 2) & 0x0F;
    int g4 = ((b0 & 0x03) << 2) | ((b1 >> 4) & 0x03);
    int b4 = b1 & 0x0F;
    c->palette[start + i][0] = (uint8_t)((r4 << 4) | r4);
    c->palette[start + i][1] = (uint8_t)((g4 << 4) | g4);
    c->palette[start + i][2] = (uint8_t)((b4 << 4) | b4);
  }
  c->changed = 1;
}

static void tile_block(cdg_t *c, const uint8_t *d, int xor_mode) {
  int color0 = d[0] & 0x0F;
  int color1 = d[1] & 0x0F;
  int row = d[2] & 0x1F;
  int col = d[3] & 0x3F;
  if (row >= TILE_ROWS || col >= TILE_COLS) return;

  int base_x = col * TILE_W, base_y = row * TILE_H;
  for (int r = 0; r < TILE_H; r++) {
    int bits = d[4 + r];
    uint8_t *line = &c->pixels[(base_y + r) * CDG_WIDTH + base_x];
    for (int x = 0; x < TILE_W; x++) {
      int color = ((bits >> (5 - x)) & 1) ? color1 : color0;
      line[x] = (uint8_t)(xor_mode ? (line[x] ^ color) : color);
    }
  }
  c->changed = 1;
}

/* Rolagem de um bloco inteiro (6 ou 12 pixels). "copy" faz a área que sai de
 * um lado entrar pelo outro; "preset" preenche a área exposta com `color`.
 * Mesmo sentido do web player: comando 1 = conteúdo vai para a
 * esquerda/cima, 2 = para a direita/baixo. */
static void scroll(cdg_t *c, const uint8_t *d, int copy) {
  int color = d[0] & 0x0F;
  int h = (d[1] >> 4) & 0x03;
  int v = (d[2] >> 4) & 0x03;
  int dx = h == 1 ? -TILE_W : h == 2 ? TILE_W : 0;
  int dy = v == 1 ? -TILE_H : v == 2 ? TILE_H : 0;
  if (dx == 0 && dy == 0) return;

  static uint8_t tmp[CDG_WIDTH * CDG_HEIGHT];
  for (int y = 0; y < CDG_HEIGHT; y++) {
    int sy = y - dy;
    for (int x = 0; x < CDG_WIDTH; x++) {
      int sx = x - dx;
      uint8_t value;
      if (copy) {
        sx = (sx + CDG_WIDTH) % CDG_WIDTH;
        sy = (sy + CDG_HEIGHT) % CDG_HEIGHT;
        value = c->pixels[sy * CDG_WIDTH + sx];
      } else if (sx < 0 || sx >= CDG_WIDTH || sy < 0 || sy >= CDG_HEIGHT) {
        value = (uint8_t)color;
      } else {
        value = c->pixels[sy * CDG_WIDTH + sx];
      }
      tmp[y * CDG_WIDTH + x] = value;
    }
  }
  memcpy(c->pixels, tmp, sizeof tmp);
  c->changed = 1;
}

static void process_packet(cdg_t *c, const uint8_t *p) {
  if ((p[0] & CDG_MASK) != CDG_COMMAND) return;
  const uint8_t *d = p + 4;
  switch (p[1] & CDG_MASK) {
    case INS_MEMORY_PRESET: memory_preset(c, d); break;
    case INS_BORDER_PRESET: border_preset(c, d); break;
    case INS_TILE_BLOCK: tile_block(c, d, 0); break;
    case INS_TILE_BLOCK_XOR: tile_block(c, d, 1); break;
    case INS_SCROLL_PRESET: scroll(c, d, 0); break;
    case INS_SCROLL_COPY: scroll(c, d, 1); break;
    case INS_LOAD_CLUT_LOW: load_clut(c, d, 0); break;
    case INS_LOAD_CLUT_HIGH: load_clut(c, d, 8); break;
    case INS_TRANSPARENT_COLOR: break; /* sem uso numa tela opaca */
    default: break;
  }
}

int cdg_update(cdg_t *c, double seconds) {
  if (c->packet_count == 0) return 0;
  long target = seconds <= 0 ? 0 : (long)(seconds * PACKETS_PER_SECOND);
  if (target > (long)c->packet_count - 1) target = (long)c->packet_count - 1;

  if (target < c->last_index) cdg_reset(c); /* seek para trás */
  for (long i = c->last_index + 1; i <= target; i++)
    process_packet(c, c->data + (size_t)i * PACKET_SIZE);
  c->last_index = target;
  return c->changed;
}

void cdg_render_rgba(cdg_t *c, uint8_t *out) {
  for (int i = 0; i < CDG_WIDTH * CDG_HEIGHT; i++) {
    const uint8_t *rgb = c->palette[c->pixels[i]];
    out[i * 4 + 0] = rgb[0];
    out[i * 4 + 1] = rgb[1];
    out[i * 4 + 2] = rgb[2];
    out[i * 4 + 3] = 255;
  }
  c->changed = 0;
}

void cdg_border_rgb(const cdg_t *c, uint8_t rgb[3]) {
  memcpy(rgb, c->palette[c->border_color], 3);
}
