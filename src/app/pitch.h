/* PlayKaraoke PS5 — troca de tom em tempo real, sem mudar a velocidade.
 *
 * Portado do js/pitch-worklet-processor.js do PlayKaraoke Web Player (mesmo
 * autor). Técnica "delay-line com dois grãos cruzados": um buffer circular
 * com o áudio recente é lido em dois pontos que se afastam da escrita na
 * taxa (ratio - 1); quando um grão precisa pular de volta, o outro está no
 * meio do ciclo, e uma janela triangular cruza os dois para esconder o pulo.
 * Sem dependências; processa estéreo intercalado em float. */

#pragma once

#define PITCH_GRAIN 2048
#define PITCH_BUFFER (PITCH_GRAIN * 2)
#define PITCH_MAX_SEMITONES 12

typedef struct pitch {
  float ratio; /* 2^(semitons/12); 1 = sem efeito */
  float delay[2][PITCH_BUFFER];
  int write_index;
  float d1, d2; /* distância de leitura de cada grão */
} pitch_t;

void pitch_init(pitch_t *p);

/* Esvazia o buffer (ex.: depois de um seek), mantendo o tom atual. */
void pitch_reset(pitch_t *p);

/* -12..+12 semitons. */
void pitch_set_semitones(pitch_t *p, int semitones);

/* Processa `frames` quadros estéreo intercalados (L,R,L,R...) no lugar. */
void pitch_process(pitch_t *p, float *samples, int frames);
