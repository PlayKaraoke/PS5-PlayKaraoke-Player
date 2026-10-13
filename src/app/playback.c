/* PlayKaraoke PS5 — tela TOCANDO. Ver playback.h. */

#include "playback.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <libswscale/swscale.h>

#include "cdg.h"
#include "i18n.h"
#include "kzip.h"
#include "online.h"
#include "pitch.h"
#include "player.h"

#define CACHE_AUDIO "/data/playkaraoke/cache/current"

#define VOLUME_STEP 5
#define REPEAT_DELAY_MS 400 /* segurar a seta/analógico repete depois disso */
#define REPEAT_EVERY_MS 100
#define STICK_ON 16000 /* analógico conta como "apertado" acima disso */
#define STICK_OFF 12000
#define SEEK_STEP_SEC 10
#define SEEK_REPEAT_MS 500

/* Desenha a tela do CDG (300x216) ampliada, centralizada pela altura, com a
 * cor de borda nas laterais. */
typedef struct cdg_view {
  SDL_Surface *surface;
  struct SwsContext *sws;
  uint8_t rgba[CDG_WIDTH * CDG_HEIGHT * 4];
  int x, y, w, h;
} cdg_view_t;

static int cdg_view_init(cdg_view_t *v, SDL_Window *win) {
  memset(v, 0, sizeof *v);
  v->surface = SDL_GetWindowSurface(win);
  int fmt = player_av_pixfmt(v->surface->format->format);
  v->h = v->surface->h;
  v->w = (v->h * CDG_WIDTH / CDG_HEIGHT) & ~1;
  v->x = (v->surface->w - v->w) / 2;
  v->y = 0;
  /* Suave como o modo 'smooth' do web player. */
  v->sws = sws_getContext(CDG_WIDTH, CDG_HEIGHT, AV_PIX_FMT_RGBA, v->w, v->h, fmt,
                          SWS_BICUBIC, NULL, NULL, NULL);
  return v->sws ? 0 : -1;
}

static void cdg_view_draw(cdg_view_t *v, cdg_t *c) {
  uint8_t border[3];
  cdg_border_rgb(c, border);
  Uint32 color = SDL_MapRGB(v->surface->format, border[0], border[1], border[2]);
  SDL_Rect left = {0, 0, v->x, v->surface->h};
  SDL_Rect right = {v->x + v->w, 0, v->surface->w - v->x - v->w, v->surface->h};
  SDL_FillRect(v->surface, &left, color);
  SDL_FillRect(v->surface, &right, color);

  cdg_render_rgba(c, v->rgba);
  const uint8_t *src[4] = {v->rgba};
  int src_stride[4] = {CDG_WIDTH * 4};
  if (SDL_MUSTLOCK(v->surface)) SDL_LockSurface(v->surface);
  uint8_t *dst[4] = {(uint8_t *)v->surface->pixels + v->y * v->surface->pitch + v->x * 4};
  int dst_stride[4] = {v->surface->pitch};
  sws_scale(v->sws, src, src_stride, 0, CDG_HEIGHT, dst, dst_stride);
  if (SDL_MUSTLOCK(v->surface)) SDL_UnlockSurface(v->surface);
}

/* Música tocando: o player e, no caso do ZIP, o CDG por cima do áudio. */
typedef struct song {
  SDL_Window *win;
  osd_t *osd;
  player_t *p;
  cdg_t *cdg;       /* NULL no MP4 */
  cdg_view_t *view;
  int *semitones;
  int *volume;
  const playback_overlay_t *overlay;
  int overlay_open;
  int overlay_dirty;
} song_t;

/* Desenha de novo a imagem da música (para apagar ou trocar o OSD). */
static int song_redraw(song_t *s) {
  if (s->cdg) {
    cdg_view_draw(s->view, s->cdg);
    return 1;
  }
  return player_redraw(s->p);
}

static void change_volume(song_t *s, int dir) {
  int v = *s->volume + dir * VOLUME_STEP;
  *s->volume = v < 0 ? 0 : v > 100 ? 100 : v;
  player_set_volume(s->p, *s->volume / 100.0f);
  osd_show_volume(s->osd, *s->volume);
}

static void change_pitch(song_t *s, int dir) {
  int st = *s->semitones + dir;
  if (st >= -PITCH_MAX_SEMITONES && st <= PITCH_MAX_SEMITONES) {
    *s->semitones = st;
    player_set_pitch(s->p, st);
  }
  osd_show_pitch(s->osd, *s->semitones); /* no limite: só mostra */
}

static void seek(song_t *s, int dir) {
  player_seek(s->p, dir * SEEK_STEP_SEC);
  osd_show_seek(s->osd, player_position(s->p), player_duration(s->p));
}

typedef struct input {
  int dpad_dir, stick_dir; /* volume: +1 cima, -1 baixo, 0 solto */
  Uint32 next_repeat;
  int seek_dir;            /* +1 direita, -1 esquerda, 0 solto */
  Uint32 next_seek;
} input_t;

static void hold_start(song_t *s, input_t *in, int dir) {
  change_volume(s, dir);
  in->next_repeat = SDL_GetTicks() + REPEAT_DELAY_MS;
}

/* Converte um eixo do analógico em -1/0/+1, com folga para não oscilar. */
static int stick_dir(int value, int current) {
  if (value < -STICK_ON) return -1;
  if (value > STICK_ON) return 1;
  if (value > -STICK_OFF && value < STICK_OFF) return 0;
  return current;
}

static void open_overlay(song_t *s, input_t *in) {
  if (!s->overlay) return;
  memset(in, 0, sizeof *in);
  s->overlay_open = 1;
  s->overlay_dirty = 1;
  player_set_hidden(s->p, 1);
  s->overlay->open(s->overlay->ctx);
}

static void close_overlay(song_t *s) {
  s->overlay_open = 0;
  player_set_hidden(s->p, 0);
  /* Volta a imagem da música (e o OSD, se houver) por cima da fila. */
  song_redraw(s);
  if (s->overlay->decorate) s->overlay->decorate(s->overlay->ctx);
  if (osd_visible(s->osd)) osd_draw(s->osd);
  SDL_UpdateWindowSurface(s->win);
}

/* Devolve 1 quando o dono encerra a música. */
static int handle_events(song_t *s, input_t *in) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (s->overlay_open && e.type != SDL_QUIT) {
      if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
      if (s->overlay->event(s->overlay->ctx, &e)) close_overlay(s);
      else s->overlay_dirty = 1;
      continue;
    }
    switch (e.type) {
      case SDL_QUIT:
        return 1;
      case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(e.cdevice.which);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        switch (e.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_A: /* ✕ */
            player_toggle_pause(s->p);
            osd_set_paused(s->osd, !s->osd->paused);
            break;
          case SDL_CONTROLLER_BUTTON_B: /* ○ */
            return 1;
          /* No SDL do PS5, Options chega como BACK e o touchpad como START. */
          case SDL_CONTROLLER_BUTTON_BACK:
            open_overlay(s, in);
            break;
          case SDL_CONTROLLER_BUTTON_START:
            osd_show_controls(s->osd);
            break;
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: /* L1: −1 semitom */
            change_pitch(s, -1);
            break;
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: /* R1: +1 semitom */
            change_pitch(s, 1);
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            in->dpad_dir = 1;
            hold_start(s, in, 1);
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            in->dpad_dir = -1;
            hold_start(s, in, -1);
            break;
        }
        break;
      case SDL_CONTROLLERTOUCHPADDOWN: /* passar a mão no touchpad */
        osd_show_controls(s->osd);
        break;
      case SDL_CONTROLLERBUTTONUP:
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
            e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
          in->dpad_dir = 0;
        break;
      case SDL_CONTROLLERAXISMOTION:
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
          /* No SDL o eixo Y cresce para baixo: inverte para +1 = cima. */
          int dir = -stick_dir(e.caxis.value, -in->stick_dir);
          if (dir != in->stick_dir) {
            in->stick_dir = dir;
            if (dir) hold_start(s, in, dir);
          }
        } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) {
          int dir = stick_dir(e.caxis.value, in->seek_dir);
          if (dir != in->seek_dir) {
            in->seek_dir = dir;
            if (dir) {
              seek(s, dir);
              in->next_seek = SDL_GetTicks() + SEEK_REPEAT_MS;
            }
          }
        }
        break;
    }
  }

  Uint32 now = SDL_GetTicks();
  int held = in->dpad_dir ? in->dpad_dir : in->stick_dir;
  if (held && (Sint32)(now - in->next_repeat) >= 0) {
    change_volume(s, held);
    in->next_repeat = now + REPEAT_EVERY_MS;
  }
  if (in->seek_dir && (Sint32)(now - in->next_seek) >= 0) {
    seek(s, in->seek_dir);
    in->next_seek = now + SEEK_REPEAT_MS;
  }
  return 0;
}

static playback_result_t run_song(song_t *s) {
  input_t in = {0};
  player_set_volume(s->p, *s->volume / 100.0f);
  if (*s->semitones) player_set_pitch(s->p, *s->semitones);
  osd_set_paused(s->osd, 0);

  player_status_t status = PLAYER_RUNNING;
  int stopped = 0;
  Uint32 last_overlay = 0;
  while (status == PLAYER_RUNNING) {
    if (handle_events(s, &in)) {
      stopped = 1;
      break;
    }

    if (s->overlay && s->overlay->tick) {
      playback_remote_t remote = {0};
      s->overlay->tick(s->overlay->ctx, &remote);
      if (remote.set_pitch && remote.semitones != *s->semitones) {
        *s->semitones = remote.semitones;
        player_set_pitch(s->p, remote.semitones);
        if (!s->overlay_open) osd_show_pitch(s->osd, remote.semitones);
      }
      if (remote.finish) {
        stopped = 1;
        break;
      }
    }

    int drew = 0;
    status = player_step(s->p, &drew);

    if (s->overlay_open) {
      /* Tela da fila por cima: a música segue, só não é desenhada. */
      if (s->cdg) cdg_update(s->cdg, player_position(s->p));
      Uint32 now = SDL_GetTicks();
      if (s->overlay_dirty || (s->overlay->animating(s->overlay->ctx) && now - last_overlay >= 250)) {
        s->overlay->draw(s->overlay->ctx);
        SDL_UpdateWindowSurface(s->win);
        s->overlay_dirty = 0;
        last_overlay = now;
      }
      SDL_Delay(4);
      continue;
    }

    if (s->cdg && cdg_update(s->cdg, player_position(s->p))) {
      cdg_view_draw(s->view, s->cdg);
      drew = 1;
    }

    /* OSD mudou e a imagem não foi redesenhada: refaz a imagem antes. */
    if (osd_changed(s->osd) && !drew) drew = song_redraw(s);
    if (drew) {
      if (s->overlay && s->overlay->decorate) s->overlay->decorate(s->overlay->ctx);
      if (osd_visible(s->osd)) osd_draw(s->osd);
      SDL_UpdateWindowSurface(s->win);
    }
    SDL_Delay(2);
  }
  osd_set_paused(s->osd, 0);
  printf("[playback] fim em %.1fs (%s)\n", player_position(s->p),
         stopped ? "encerrada" : status == PLAYER_ERROR ? "erro" : "ok");
  return stopped ? PLAYBACK_STOPPED : PLAYBACK_ENDED;
}

static playback_result_t play_mp4(song_t *s, const char *path, char *error, size_t size) {
  if (!(s->p = player_open(path, s->win, 0))) {
    snprintf(error, size, TR("Couldn't open the video.", "Não consegui abrir o vídeo."));
    return PLAYBACK_FAILED;
  }
  playback_result_t r = run_song(s);
  player_close(s->p);
  return r;
}

static playback_result_t play_zip(song_t *s, const char *path, char *error, size_t size) {
  uint8_t *cdg_data = NULL;
  size_t cdg_size = 0;
  char audio_path[512] = "";
  if (kzip_extract(path, &cdg_data, &cdg_size, CACHE_AUDIO, audio_path, sizeof audio_path) != 0) {
    snprintf(error, size, TR("The ZIP must contain a .cdg and an audio file (.mp3).", "O ZIP precisa ter um .cdg e um áudio (.mp3)."));
    return PLAYBACK_FAILED;
  }
  printf("[playback] cdg %zu bytes (%.1fs)\n", cdg_size, cdg_size / 24.0 / 300.0);

  playback_result_t r = PLAYBACK_FAILED;
  s->cdg = malloc(sizeof(cdg_t));
  s->view = malloc(sizeof(cdg_view_t));
  if (!s->cdg || !s->view || cdg_view_init(s->view, s->win) != 0 ||
      !(s->p = player_open(audio_path, s->win, 1))) {
    snprintf(error, size, TR("Couldn't play the ZIP.", "Não consegui tocar o ZIP."));
  } else {
    cdg_init(s->cdg, cdg_data, cdg_size);
    r = run_song(s);
  }

  player_close(s->p);
  if (s->view && s->view->sws) sws_freeContext(s->view->sws);
  free(s->view);
  free(s->cdg);
  free(cdg_data);
  unlink(audio_path);
  return r;
}

int sceSystemServiceLaunchWebBrowser(const char *uri, void *param);

static void draw_youtube_wait(ui_t *ui, const char *display) {
  ui_clear(ui, UI_BG);
  ui_rect(ui, 0, 0, UI_W, 6, UI_BRAND);
  ui_logo(ui, UI_W / 2, 90);
  ui_text(ui, FONT_SMALL, TR("PLAYING ON YOUTUBE", "TOCANDO NO YOUTUBE"), UI_W / 2, 420, UI_TEXT_3, ALIGN_CENTER, 0);
  ui_text(ui, FONT_HEADING, display, UI_W / 2, 460, UI_TEXT, ALIGN_CENTER, 1600);
  ui_text(ui, FONT_BODY, TR("The video opened in the PS5 web browser.", "O vídeo abriu no navegador do PS5."),
          UI_W / 2, 580, UI_TEXT_2, ALIGN_CENTER, 1600);
  ui_text(ui, FONT_BODY,
          TR("When it ends, close the browser with the circle button and press X for the next singer.",
             "Quando acabar, feche o navegador com a bolinha e aperte X para o próximo cantor."),
          UI_W / 2, 634, UI_TEXT_2, ALIGN_CENTER, 1700);
  const ui_button_t b[] = {BTN_CROSS, BTN_TRIANGLE, BTN_OPTIONS};
  const char *const l[] = {TR("Next singer", "Próximo cantor"), TR("Open the video again", "Abrir o vídeo de novo"),
                           TR("Queue", "Fila")};
  ui_footer(ui, b, l, 3);
}

/* Música do YouTube: abre no navegador do PS5 (player oficial) e espera o
 * dono chamar o próximo (✕) ou o cantor terminar pelo celular. */
static playback_result_t play_youtube(song_t *s, const char *path, const char *display) {
  char url[256];
  online_watch_url(path, url, sizeof url);
  printf("[playback] youtube: %s\n", url);
  int rc = sceSystemServiceLaunchWebBrowser(url, NULL);
  if (rc) printf("[playback] sceSystemServiceLaunchWebBrowser: 0x%08x\n", (unsigned)rc);

  ui_t *ui = s->osd->ui;
  int dirty = 1;
  Uint32 last_overlay = 0;
  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) return PLAYBACK_STOPPED;
      if (s->overlay_open) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
        if (s->overlay->event(s->overlay->ctx, &e)) {
          s->overlay_open = 0;
          dirty = 1;
        } else {
          s->overlay_dirty = 1;
        }
        continue;
      }
      if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
      if (e.type != SDL_CONTROLLERBUTTONDOWN) continue;
      if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) return PLAYBACK_ENDED; /* ✕ */
      if (e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) sceSystemServiceLaunchWebBrowser(url, NULL); /* △ */
      if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK && s->overlay) { /* OPTIONS */
        s->overlay_open = s->overlay_dirty = 1;
        s->overlay->open(s->overlay->ctx);
      }
    }
    if (s->overlay && s->overlay->tick) {
      playback_remote_t remote = {0};
      s->overlay->tick(s->overlay->ctx, &remote);
      if (remote.finish) return PLAYBACK_STOPPED;
    }
    Uint32 now = SDL_GetTicks();
    if (s->overlay_open) {
      if (s->overlay_dirty || (s->overlay->animating(s->overlay->ctx) && now - last_overlay >= 250)) {
        s->overlay->draw(s->overlay->ctx);
        SDL_UpdateWindowSurface(s->win);
        s->overlay_dirty = 0;
        last_overlay = now;
      }
    } else if (dirty) {
      draw_youtube_wait(ui, display);
      SDL_UpdateWindowSurface(s->win);
      dirty = 0;
    }
    SDL_Delay(30);
  }
}

playback_result_t playback_play(SDL_Window *win, osd_t *osd, const playback_overlay_t *overlay,
                                const char *path, const char *display, int *semitones, int *volume,
                                char *error, size_t error_size) {
  printf("[playback] tocando %s (tom %+d)\n", path, *semitones);
  song_t s = {win, osd, NULL, NULL, NULL, semitones, volume, overlay, 0, 0};
  error[0] = '\0';
  if (online_is_path(path)) return play_youtube(&s, path, display);
  size_t len = strlen(path);
  if (len > 4 && strcasecmp(path + len - 4, ".zip") == 0) return play_zip(&s, path, error, error_size);
  return play_mp4(&s, path, error, error_size);
}
