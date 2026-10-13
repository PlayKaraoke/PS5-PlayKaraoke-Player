/* PlayKaraoke PS5 — avisos rápidos por cima da música (OSD): volume, tempo
 * da música (ao avançar/voltar) e tom, que somem sozinhos, e "Pausado", que
 * fica enquanto durar a pausa.
 *
 * O OSD é semitransparente, então não pode ser desenhado duas vezes sobre o
 * mesmo quadro: quando ele muda (osd_changed), o app redesenha a imagem da
 * música primeiro e só depois chama osd_draw. */

#pragma once

#include "ui.h"

typedef enum { OSD_TOAST_NONE, OSD_TOAST_VOLUME, OSD_TOAST_SEEK, OSD_TOAST_PITCH } osd_toast_t;

typedef struct osd {
  ui_t *ui;
  osd_toast_t toast;   /* aviso temporário da vez (só um por vez) */
  Uint32 toast_until;  /* SDL_GetTicks até quando ele aparece */
  int volume_pct;
  double seek_pos, seek_duration;
  int semitones;
  int paused;
  Uint32 controls_until; /* barra de comandos (touchpad) */
  int was_visible;
  int dirty;
} osd_t;

void osd_init(osd_t *o, ui_t *ui);

void osd_show_volume(osd_t *o, int pct);
void osd_show_seek(osd_t *o, double pos, double duration);
void osd_show_pitch(osd_t *o, int semitones);
void osd_set_paused(osd_t *o, int paused);

/* Barra pequena embaixo com os comandos da música (some sozinha). */
void osd_show_controls(osd_t *o);

/* Atualiza o estado (os avisos somem depois de um tempo). Devolve 1 se o
 * que está na tela precisa ser refeito: redesenhar a música e depois
 * osd_draw. */
int osd_changed(osd_t *o);
int osd_visible(const osd_t *o);
void osd_draw(osd_t *o);
