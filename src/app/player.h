/* PlayKaraoke PS5 — player de vídeo (MP4) com FFmpeg.
 *
 * O áudio é o relógio: cada quadro de vídeo aparece quando o áudio chega no
 * tempo dele. O vídeo é convertido e desenhado direto na superfície da
 * janela (o SDL do PS5 não tem renderer acelerado). */

#pragma once

#include <SDL.h>

typedef struct player player_t;

typedef enum {
  PLAYER_RUNNING = 0,
  PLAYER_ENDED = 1,
  PLAYER_ERROR = -1,
} player_status_t;

/* Abre o arquivo e prepara áudio e vídeo. Com audio_only, ignora o vídeo
 * (usado no CDG: o MP3 pode ter capa, que o FFmpeg vê como vídeo).
 * NULL em caso de erro (já logado). */
player_t *player_open(const char *path, SDL_Window *window, int audio_only);

/* Chamar em loop: decodifica, enfileira áudio e desenha o quadro da vez na
 * superfície. *drew = 1 quando desenhou: cabe ao app chamar
 * SDL_UpdateWindowSurface (depois de desenhar o que for por cima). */
player_status_t player_step(player_t *p, int *drew);

/* Com hidden, o vídeo continua (e o áudio toca) mas nada é desenhado:
 * usado quando a tela da fila está por cima. */
void player_set_hidden(player_t *p, int hidden);

/* Redesenha o último quadro (ex.: para apagar o OSD com o vídeo pausado).
 * Devolve 1 se havia quadro. */
int player_redraw(player_t *p);

/* Duração em segundos (0 se desconhecida). */
double player_duration(const player_t *p);

/* Avança (delta > 0) ou volta (delta < 0) a música, em segundos. */
void player_seek(player_t *p, double delta);

/* Tom em semitons (-12..+12), sem mudar a velocidade, aplicado na hora. */
void player_set_pitch(player_t *p, int semitones);

/* Volume de 0 a 1, aplicado na hora (curva quadrática). */
void player_set_volume(player_t *p, float volume);

void player_toggle_pause(player_t *p);
double player_position(const player_t *p);
void player_close(player_t *p);

/* Formato do FFmpeg (AVPixelFormat) equivalente ao da superfície SDL, ou -1. */
int player_av_pixfmt(Uint32 sdl_format);
