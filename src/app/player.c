/* PlayKaraoke PS5 — player de vídeo (MP4) com FFmpeg. Ver player.h. */

#include "player.h"
#include "pitch.h"

#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define OUT_RATE 48000
#define OUT_CHANNELS 2
#define OUT_BYTES_PER_SAMPLE (2 * OUT_CHANNELS) /* S16 estéreo */

#define AUDIO_AHEAD_SEC 0.3 /* quanto áudio decodificado manter pronto */
#define RING_FRAMES (OUT_RATE * 2) /* capacidade do buffer circular: 2 s */
#define VIDEO_QUEUE_MAX 32  /* quadros decodificados esperando a vez */
#define MIX_FRAMES 2048     /* bloco processado de cada vez no callback */

struct player {
  SDL_Window *window;
  SDL_Surface *surface;

  AVFormatContext *fmt;
  AVPacket *pkt;
  AVFrame *frame;
  int eof;

  /* vídeo */
  int vidx;
  AVCodecContext *vdec;
  struct SwsContext *sws;
  int dst_x, dst_y, dst_w, dst_h;
  enum AVPixelFormat dst_fmt;
  AVFrame *vq[VIDEO_QUEUE_MAX];
  int vq_count;

  /* áudio */
  int aidx;
  AVCodecContext *adec;
  SwrContext *swr;
  SDL_AudioDeviceID dev;
  uint8_t *abuf;
  int abuf_size;
  double audio_start; /* tempo (s) da primeira amostra enfileirada (-1 = ainda não) */
  double seek_target; /* depois de um seek: posição até o áudio novo chegar */

  /* Buffer circular entre o decodificador e o callback do SDL. Os campos
   * abaixo só são mexidos com SDL_LockAudioDevice (ou dentro do callback). */
  int16_t *ring;
  int ring_read, ring_write, ring_fill; /* em quadros estéreo */
  int64_t frames_played;
  float volume;  /* 0..1, pedido pelo app */
  float gain;    /* ganho aplicado agora (rampa até volume²) */
  pitch_t pitch; /* troca de tom, aplicada na saída */
  float mix[MIX_FRAMES * OUT_CHANNELS]; /* rascunho em float do callback */

  /* último quadro mostrado, para redesenhar (ex.: tirar o OSD na pausa) */
  AVFrame *last;
  int drew;
  int hidden; /* tela da fila por cima: guarda os quadros sem desenhar */

  /* relógio sem áudio */
  Uint64 wall_start;
  double wall_paused_at;
  int paused;

  /* estatísticas */
  int shown, dropped;
  Uint32 last_report;
};

static void log_err(const char *what, int err) {
  char buf[128];
  av_strerror(err, buf, sizeof buf);
  printf("[player] %s: %s\n", what, buf);
}

static AVCodecContext *open_decoder(AVStream *st, int threads) {
  const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec) {
    printf("[player] sem decodificador para %s\n", avcodec_get_name(st->codecpar->codec_id));
    return NULL;
  }
  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  if (!ctx) return NULL;
  avcodec_parameters_to_context(ctx, st->codecpar);
  ctx->pkt_timebase = st->time_base;
  if (threads) {
    /* A detecção automática de núcleos não é confiável no PS5. */
    ctx->thread_count = threads;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  }
  int err = avcodec_open2(ctx, codec, NULL);
  if (err < 0) {
    log_err("avcodec_open2", err);
    avcodec_free_context(&ctx);
    return NULL;
  }
  return ctx;
}

int player_av_pixfmt(Uint32 sdl_format) {
  switch (sdl_format) {
    case SDL_PIXELFORMAT_ARGB8888: return AV_PIX_FMT_BGRA;
    case SDL_PIXELFORMAT_XRGB8888: return AV_PIX_FMT_BGR0;
    case SDL_PIXELFORMAT_ABGR8888: return AV_PIX_FMT_RGBA;
    case SDL_PIXELFORMAT_XBGR8888: return AV_PIX_FMT_RGB0;
    default: return AV_PIX_FMT_NONE;
  }
}

/* Encaixa o vídeo na tela mantendo a proporção (tarjas pretas). */
static void compute_fit(player_t *p) {
  AVRational sar = av_guess_sample_aspect_ratio(p->fmt, p->fmt->streams[p->vidx], NULL);
  double aspect = (double)p->vdec->width / p->vdec->height;
  if (sar.num > 0 && sar.den > 0) aspect *= av_q2d(sar);

  int sw = p->surface->w, sh = p->surface->h;
  int w = sw, h = (int)(sw / aspect + 0.5);
  if (h > sh) {
    h = sh;
    w = (int)(sh * aspect + 0.5);
  }
  p->dst_w = w & ~1;
  p->dst_h = h & ~1;
  p->dst_x = (sw - p->dst_w) / 2;
  p->dst_y = (sh - p->dst_h) / 2;
}

/* Callback do SDL (thread de áudio): copia do buffer circular aplicando a
 * troca de tom e o volume. A curva do volume é quadrática (mais natural ao
 * ouvido) e o ganho muda em rampa ao longo do bloco, para não estalar. Sem
 * dados, toca silêncio e o relógio para. */
static void audio_callback(void *userdata, Uint8 *stream, int len) {
  player_t *p = userdata;
  int16_t *out = (int16_t *)stream;
  int frames = len / OUT_BYTES_PER_SAMPLE;
  int n = frames < p->ring_fill ? frames : p->ring_fill;

  float target = p->volume * p->volume;
  float g = p->gain;
  float step = n > 0 ? (target - g) / n : 0;
  for (int done = 0; done < n;) {
    int chunk = n - done < MIX_FRAMES ? n - done : MIX_FRAMES;
    for (int i = 0; i < chunk; i++) {
      const int16_t *in = &p->ring[((p->ring_read + done + i) % RING_FRAMES) * OUT_CHANNELS];
      p->mix[i * 2] = in[0];
      p->mix[i * 2 + 1] = in[1];
    }
    pitch_process(&p->pitch, p->mix, chunk);
    for (int i = 0; i < chunk; i++) {
      g += step;
      for (int ch = 0; ch < OUT_CHANNELS; ch++) {
        float v = p->mix[i * 2 + ch] * g;
        out[(done + i) * OUT_CHANNELS + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
      }
    }
    done += chunk;
  }
  p->gain = target;
  p->ring_read = (p->ring_read + n) % RING_FRAMES;
  p->ring_fill -= n;
  p->frames_played += n;
  if (n < frames) memset(out + n * OUT_CHANNELS, 0, (size_t)(frames - n) * OUT_BYTES_PER_SAMPLE);
}

static int open_audio(player_t *p) {
  if (!(p->ring = malloc((size_t)RING_FRAMES * OUT_BYTES_PER_SAMPLE))) return -1;
  p->volume = 1.0f;
  p->gain = 1.0f;
  pitch_init(&p->pitch);

  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = OUT_RATE;
  want.format = AUDIO_S16SYS;
  want.channels = OUT_CHANNELS;
  want.samples = 1024;
  want.callback = audio_callback;
  want.userdata = p;
  p->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!p->dev) {
    printf("[player] SDL_OpenAudioDevice: %s\n", SDL_GetError());
    return -1;
  }
  if (have.freq != OUT_RATE || have.channels != OUT_CHANNELS || have.format != AUDIO_S16SYS) {
    printf("[player] audio aberto com formato diferente (%d Hz, %d canais)\n", have.freq, have.channels);
    return -1;
  }

  AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
  int err = swr_alloc_set_opts2(&p->swr, &out_layout, AV_SAMPLE_FMT_S16, OUT_RATE,
                                &p->adec->ch_layout, p->adec->sample_fmt,
                                p->adec->sample_rate, 0, NULL);
  if (err < 0 || (err = swr_init(p->swr)) < 0) {
    log_err("swr_init", err);
    return -1;
  }
  p->audio_start = -1;
  return 0;
}

player_t *player_open(const char *path, SDL_Window *window, int audio_only) {
  player_t *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->window = window;
  p->surface = SDL_GetWindowSurface(window);
  p->vidx = p->aidx = -1;

  int err = avformat_open_input(&p->fmt, path, NULL, NULL);
  if (err < 0) {
    log_err("avformat_open_input", err);
    goto fail;
  }
  if ((err = avformat_find_stream_info(p->fmt, NULL)) < 0) {
    log_err("avformat_find_stream_info", err);
    goto fail;
  }
  p->vidx = av_find_best_stream(p->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  p->aidx = av_find_best_stream(p->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  /* Capa de álbum (MP3) não é vídeo de verdade. */
  if (p->vidx >= 0 &&
      (audio_only || (p->fmt->streams[p->vidx]->disposition & AV_DISPOSITION_ATTACHED_PIC)))
    p->vidx = -1;
  if (p->vidx < 0 && p->aidx < 0) {
    printf("[player] arquivo sem audio nem video\n");
    goto fail;
  }

  if (p->vidx >= 0) {
    if (!(p->vdec = open_decoder(p->fmt->streams[p->vidx], 8))) goto fail;
    printf("[player] video %s %dx%d %s\n", avcodec_get_name(p->vdec->codec_id),
           p->vdec->width, p->vdec->height, av_get_pix_fmt_name(p->vdec->pix_fmt));

    if (!p->surface) {
      printf("[player] sem superficie: %s\n", SDL_GetError());
      goto fail;
    }
    p->dst_fmt = player_av_pixfmt(p->surface->format->format);
    if (p->dst_fmt == AV_PIX_FMT_NONE) {
      printf("[player] formato de superficie nao suportado: %s\n",
             SDL_GetPixelFormatName(p->surface->format->format));
      goto fail;
    }
    compute_fit(p);
    printf("[player] tela %dx%d %s, video em %dx%d+%d+%d\n", p->surface->w, p->surface->h,
           SDL_GetPixelFormatName(p->surface->format->format), p->dst_w, p->dst_h, p->dst_x, p->dst_y);
  }

  if (p->aidx >= 0) {
    if (!(p->adec = open_decoder(p->fmt->streams[p->aidx], 0)) || open_audio(p) != 0) {
      printf("[player] seguindo sem audio\n");
      if (p->dev) SDL_CloseAudioDevice(p->dev);
      p->dev = 0;
    } else {
      printf("[player] audio %s %d Hz\n", avcodec_get_name(p->adec->codec_id), p->adec->sample_rate);
    }
  }
  if (!p->vdec && !p->dev) {
    printf("[player] sem audio para tocar\n");
    goto fail;
  }

  p->pkt = av_packet_alloc();
  p->frame = av_frame_alloc();
  if (!p->pkt || !p->frame) goto fail;

  if (p->vdec) {
    SDL_FillRect(p->surface, NULL, SDL_MapRGB(p->surface->format, 0, 0, 0));
    SDL_UpdateWindowSurface(window);
  }

  p->wall_start = SDL_GetPerformanceCounter();
  p->last_report = SDL_GetTicks();
  if (p->dev) SDL_PauseAudioDevice(p->dev, 0);
  return p;

fail:
  player_close(p);
  return NULL;
}

double player_position(const player_t *p) {
  if (p->dev) {
    if (p->audio_start < 0) return p->seek_target;
    return p->audio_start + (double)p->frames_played / OUT_RATE;
  }
  if (p->paused) return p->wall_paused_at;
  return (double)(SDL_GetPerformanceCounter() - p->wall_start) / SDL_GetPerformanceFrequency();
}

static double queued_audio_sec(const player_t *p) {
  if (!p->dev) return AUDIO_AHEAD_SEC;
  return (double)p->ring_fill / OUT_RATE;
}

static void queue_audio_frame(player_t *p, AVFrame *f) {
  if (p->audio_start < 0) {
    int64_t ts = f->best_effort_timestamp;
    double t = ts == AV_NOPTS_VALUE ? p->seek_target : ts * av_q2d(p->fmt->streams[p->aidx]->time_base);
    /* Depois de um seek o demuxer volta até um ponto anterior: descarta o
     * áudio de antes do alvo. */
    double frame_end = t + (double)f->nb_samples / f->sample_rate;
    if (frame_end <= p->seek_target) return;
    p->audio_start = t;
  }
  int out_samples = swr_get_out_samples(p->swr, f->nb_samples);
  int need = out_samples * OUT_BYTES_PER_SAMPLE;
  if (need > p->abuf_size) {
    av_free(p->abuf);
    p->abuf = av_malloc(need);
    p->abuf_size = p->abuf ? need : 0;
    if (!p->abuf) return;
  }
  int got = swr_convert(p->swr, &p->abuf, out_samples, (const uint8_t **)f->extended_data, f->nb_samples);
  if (got <= 0) return;

  const int16_t *src = (const int16_t *)p->abuf;
  SDL_LockAudioDevice(p->dev);
  int space = RING_FRAMES - p->ring_fill;
  if (got > space) got = space; /* não acontece: a fila fica bem abaixo de 2 s */
  for (int i = 0; i < got; i++) {
    int16_t *dst = &p->ring[((p->ring_write + i) % RING_FRAMES) * OUT_CHANNELS];
    dst[0] = src[i * 2];
    dst[1] = src[i * 2 + 1];
  }
  p->ring_write = (p->ring_write + got) % RING_FRAMES;
  p->ring_fill += got;
  SDL_UnlockAudioDevice(p->dev);
}

static void push_video_frame(player_t *p, AVFrame *f) {
  if (p->vq_count == VIDEO_QUEUE_MAX) {
    /* Fila cheia: o quadro mais antigo já está atrasado. */
    av_frame_free(&p->vq[0]);
    memmove(p->vq, p->vq + 1, (VIDEO_QUEUE_MAX - 1) * sizeof p->vq[0]);
    p->vq_count--;
    p->dropped++;
  }
  AVFrame *copy = av_frame_alloc();
  if (!copy) return;
  av_frame_move_ref(copy, f);
  p->vq[p->vq_count++] = copy;
}

static void drain_decoder(player_t *p, AVCodecContext *dec, int is_video) {
  while (avcodec_receive_frame(dec, p->frame) == 0) {
    if (is_video) {
      push_video_frame(p, p->frame);
    } else {
      if (p->dev) queue_audio_frame(p, p->frame);
      av_frame_unref(p->frame);
    }
  }
}

/* Lê pacotes até ter áudio suficiente na fila e algum vídeo esperando. */
static void fill_queues(player_t *p) {
  while (!p->eof && (queued_audio_sec(p) < AUDIO_AHEAD_SEC || (p->vdec && p->vq_count == 0))) {
    int err = av_read_frame(p->fmt, p->pkt);
    if (err < 0) {
      /* Fim do arquivo: esvazia o que ficou dentro dos decodificadores. */
      p->eof = 1;
      if (p->vdec) {
        avcodec_send_packet(p->vdec, NULL);
        drain_decoder(p, p->vdec, 1);
      }
      if (p->adec) {
        avcodec_send_packet(p->adec, NULL);
        drain_decoder(p, p->adec, 0);
      }
      break;
    }
    if (p->vdec && p->pkt->stream_index == p->vidx) {
      if (avcodec_send_packet(p->vdec, p->pkt) == 0) drain_decoder(p, p->vdec, 1);
    } else if (p->pkt->stream_index == p->aidx && p->dev) {
      if (avcodec_send_packet(p->adec, p->pkt) == 0) drain_decoder(p, p->adec, 0);
    }
    av_packet_unref(p->pkt);
  }
}

static double frame_time(const player_t *p, const AVFrame *f) {
  int64_t ts = f->best_effort_timestamp;
  if (ts == AV_NOPTS_VALUE) return 0;
  return ts * av_q2d(p->fmt->streams[p->vidx]->time_base);
}

static void draw_frame(player_t *p, AVFrame *f) {
  if (p->hidden) return;
  p->sws = sws_getCachedContext(p->sws, f->width, f->height, f->format,
                                p->dst_w, p->dst_h, p->dst_fmt, SWS_BILINEAR, NULL, NULL, NULL);
  if (!p->sws) return;

  if (SDL_MUSTLOCK(p->surface)) SDL_LockSurface(p->surface);
  uint8_t *dst[4] = {(uint8_t *)p->surface->pixels + p->dst_y * p->surface->pitch + p->dst_x * 4};
  int dst_stride[4] = {p->surface->pitch};
  sws_scale(p->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, dst_stride);
  if (SDL_MUSTLOCK(p->surface)) SDL_UnlockSurface(p->surface);
  p->drew = 1;
}

player_status_t player_step(player_t *p, int *drew) {
  p->drew = 0;
  if (drew) *drew = 0;
  if (p->paused) return PLAYER_RUNNING;

  fill_queues(p);

  /* Mostra o quadro mais recente cujo tempo já chegou; descarta os anteriores. */
  double now = player_position(p);
  int due = -1;
  for (int i = 0; i < p->vq_count && frame_time(p, p->vq[i]) <= now; i++) due = i;
  if (due >= 0) {
    draw_frame(p, p->vq[due]);
    p->shown++;
    p->dropped += due;
    av_frame_free(&p->last);
    p->last = p->vq[due];
    for (int i = 0; i < due; i++) av_frame_free(&p->vq[i]);
    memmove(p->vq, p->vq + due + 1, (p->vq_count - due - 1) * sizeof p->vq[0]);
    p->vq_count -= due + 1;
  }

  Uint32 ticks = SDL_GetTicks();
  if (ticks - p->last_report >= 5000) {
    printf("[player] t=%.1fs quadros=%d descartados=%d fila_video=%d fila_audio=%.2fs\n",
           now, p->shown, p->dropped, p->vq_count, queued_audio_sec(p));
    p->last_report = ticks;
  }

  if (drew) *drew = p->drew;
  if (p->eof && p->vq_count == 0 && (!p->dev || p->ring_fill == 0))
    return PLAYER_ENDED;
  return PLAYER_RUNNING;
}

void player_set_hidden(player_t *p, int hidden) {
  p->hidden = hidden;
}

int player_redraw(player_t *p) {
  if (!p->last) return 0;
  draw_frame(p, p->last);
  return 1;
}

double player_duration(const player_t *p) {
  if (p->fmt->duration == AV_NOPTS_VALUE) return 0;
  return (double)p->fmt->duration / AV_TIME_BASE;
}

void player_seek(player_t *p, double delta) {
  double duration = player_duration(p);
  double target = player_position(p) + delta;
  if (duration > 0 && target > duration - 1) target = duration - 1;
  if (target < 0) target = 0;

  int64_t ts = (int64_t)(target * AV_TIME_BASE);
  int err = avformat_seek_file(p->fmt, -1, INT64_MIN, ts, ts, 0);
  if (err < 0) {
    log_err("avformat_seek_file", err);
    return;
  }

  /* Joga fora tudo o que foi decodificado antes do seek. */
  if (p->vdec) avcodec_flush_buffers(p->vdec);
  if (p->adec) avcodec_flush_buffers(p->adec);
  for (int i = 0; i < p->vq_count; i++) av_frame_free(&p->vq[i]);
  p->vq_count = 0;
  p->eof = 0;

  if (p->dev) {
    SDL_LockAudioDevice(p->dev);
    p->ring_read = p->ring_write = p->ring_fill = 0;
    p->frames_played = 0;
    p->audio_start = -1;
    p->seek_target = target;
    pitch_reset(&p->pitch);
    SDL_UnlockAudioDevice(p->dev);
    if (p->swr) swr_init(p->swr);
  } else {
    p->wall_paused_at = target;
    p->wall_start = SDL_GetPerformanceCounter() - (Uint64)(target * SDL_GetPerformanceFrequency());
  }
  printf("[player] seek %+.0fs para %.1fs\n", delta, target);
}

void player_set_pitch(player_t *p, int semitones) {
  if (!p->dev) return;
  SDL_LockAudioDevice(p->dev);
  pitch_set_semitones(&p->pitch, semitones);
  SDL_UnlockAudioDevice(p->dev);
  printf("[player] tom %+d\n", semitones);
}

void player_set_volume(player_t *p, float volume) {
  if (!p->dev) return;
  if (volume < 0) volume = 0;
  if (volume > 1) volume = 1;
  SDL_LockAudioDevice(p->dev);
  p->volume = volume;
  if (p->frames_played == 0) p->gain = volume * volume; /* sem rampa no início */
  SDL_UnlockAudioDevice(p->dev);
}

void player_toggle_pause(player_t *p) {
  p->paused = !p->paused;
  if (p->dev) {
    SDL_PauseAudioDevice(p->dev, p->paused);
  } else if (p->paused) {
    p->wall_paused_at = player_position(p);
  } else {
    p->wall_start = SDL_GetPerformanceCounter() -
                    (Uint64)(p->wall_paused_at * SDL_GetPerformanceFrequency());
  }
  printf("[player] %s em %.1fs\n", p->paused ? "pausado" : "retomado", player_position(p));
}

void player_close(player_t *p) {
  if (!p) return;
  if (p->dev) SDL_CloseAudioDevice(p->dev);
  free(p->ring);
  av_frame_free(&p->last);
  for (int i = 0; i < p->vq_count; i++) av_frame_free(&p->vq[i]);
  av_frame_free(&p->frame);
  av_packet_free(&p->pkt);
  sws_freeContext(p->sws);
  swr_free(&p->swr);
  av_free(p->abuf);
  avcodec_free_context(&p->vdec);
  avcodec_free_context(&p->adec);
  avformat_close_input(&p->fmt);
  free(p);
}
