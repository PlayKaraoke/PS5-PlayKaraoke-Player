/* PlayKaraoke PS5 — navegador de arquivos (HD externo e pasta de teste).
 *
 * Raiz: a pasta de teste e cada HD montado (/mnt/usb0…7, /mnt/ext0) que
 * tenha algo dentro. Dentro de uma pasta: subpastas e músicas (.zip, .mp4),
 * pastas primeiro, em ordem alfabética; ignora ocultos e o lixo do macOS. */

#pragma once

typedef struct bentry {
  char label[256];
  char path[1024];
  int is_dir;
} bentry_t;

typedef struct browser {
  char path[1024]; /* pasta aberta ("" = raiz) */
  char root_path[1024]; /* início do disco/pasta-raiz atual */
  bentry_t *entries;
  int count, capacity;
  int sel, scroll;
} browser_t;

void browser_init(browser_t *b);
void browser_free(browser_t *b);

/* Mostra a raiz (pasta de teste + HDs montados). */
void browser_open_root(browser_t *b);

/* Abre uma pasta. Devolve 0 se conseguiu. */
int browser_open(browser_t *b, const char *path);

/* Volta uma pasta (na pasta inicial de um disco, volta para a raiz).
 * Devolve 0 se voltou, -1 se já estava na raiz. */
int browser_up(browser_t *b);

/* Move a seleção (delta pode ser maior que 1 para "página"). */
void browser_move(browser_t *b, int delta);

const bentry_t *browser_selected(const browser_t *b);
int browser_is_root(const browser_t *b);
