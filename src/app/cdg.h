/* PlayKaraoke PS5 — decodificador CD+Graphics (.cdg).
 *
 * Portado do js/cdg-player.js do PlayKaraoke Web Player (mesmo autor).
 * Pacotes de 24 bytes, 300 por segundo, tela de 300x216 com paleta de 16
 * cores. O tempo vem do relógio do áudio: cdg_update(t) processa os pacotes
 * até t; se o tempo voltar (seek), recomeça do zero. Sem dependências. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define CDG_WIDTH 300
#define CDG_HEIGHT 216

typedef struct cdg {
  const uint8_t *data;
  size_t packet_count;
  long last_index; /* último pacote processado (-1 = nenhum) */

  uint8_t pixels[CDG_WIDTH * CDG_HEIGHT]; /* índice de cor (0-15) por pixel */
  uint8_t palette[16][3];
  int border_color;
  int changed; /* algo visível mudou desde o último cdg_render_rgba */
} cdg_t;

/* `data` precisa continuar válido enquanto o cdg_t for usado. */
void cdg_init(cdg_t *c, const uint8_t *data, size_t size);
void cdg_reset(cdg_t *c);

/* Processa os pacotes até `seconds`. Devolve 1 se a tela mudou. */
int cdg_update(cdg_t *c, double seconds);

/* Escreve a tela em RGBA (bytes R,G,B,A), CDG_WIDTH*CDG_HEIGHT*4 bytes. */
void cdg_render_rgba(cdg_t *c, uint8_t *out);

/* Cor da borda em RGB. */
void cdg_border_rgb(const cdg_t *c, uint8_t rgb[3]);
