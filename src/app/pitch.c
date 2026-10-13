/* PlayKaraoke PS5 — troca de tom. Ver pitch.h. */

#include "pitch.h"

#include <math.h>
#include <string.h>

void pitch_init(pitch_t *p) {
  memset(p, 0, sizeof *p);
  p->ratio = 1.0f;
  p->d2 = PITCH_GRAIN / 2.0f; /* segundo grão defasado meio ciclo */
}

void pitch_reset(pitch_t *p) {
  float ratio = p->ratio;
  pitch_init(p);
  p->ratio = ratio;
}

void pitch_set_semitones(pitch_t *p, int semitones) {
  if (semitones > PITCH_MAX_SEMITONES) semitones = PITCH_MAX_SEMITONES;
  if (semitones < -PITCH_MAX_SEMITONES) semitones = -PITCH_MAX_SEMITONES;
  p->ratio = powf(2.0f, semitones / 12.0f);
}

/* Janela triangular: 0 nas bordas (onde o grão pula), 1 no meio. */
static float grain_window(float d) {
  float x = d / PITCH_GRAIN;
  return 1.0f - fabsf(x - 0.5f) * 2.0f;
}

static float read_interpolated(const float *buf, int write_index, float delay) {
  float pos = write_index - delay;
  while (pos < 0) pos += PITCH_BUFFER;
  while (pos >= PITCH_BUFFER) pos -= PITCH_BUFFER;
  int i0 = (int)pos;
  int i1 = (i0 + 1) % PITCH_BUFFER;
  float frac = pos - i0;
  return buf[i0] * (1.0f - frac) + buf[i1] * frac;
}

void pitch_process(pitch_t *p, float *samples, int frames) {
  const float step = p->ratio - 1.0f;
  const int passthrough = fabsf(step) < 0.001f;
  int wi = p->write_index;
  float d1 = p->d1, d2 = p->d2;

  for (int i = 0; i < frames; i++) {
    float *frame = &samples[i * 2];
    /* O buffer é sempre alimentado, para a troca de tom entrar sem buraco. */
    p->delay[0][wi] = frame[0];
    p->delay[1][wi] = frame[1];

    if (!passthrough) {
      d1 -= step;
      d2 -= step;
      if (d1 < 0) d1 += PITCH_GRAIN;
      else if (d1 >= PITCH_GRAIN) d1 -= PITCH_GRAIN;
      if (d2 < 0) d2 += PITCH_GRAIN;
      else if (d2 >= PITCH_GRAIN) d2 -= PITCH_GRAIN;

      float w1 = grain_window(d1), w2 = grain_window(d2);
      float wsum = w1 + w2;
      for (int ch = 0; ch < 2; ch++) {
        float r1 = read_interpolated(p->delay[ch], wi, d1);
        float r2 = read_interpolated(p->delay[ch], wi, d2);
        frame[ch] = wsum > 0.0001f ? (r1 * w1 + r2 * w2) / wsum : 0.0f;
      }
    }
    wi = (wi + 1) % PITCH_BUFFER;
  }

  p->write_index = wi;
  p->d1 = d1;
  p->d2 = d2;
}
