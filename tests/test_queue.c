#include <stdio.h>
#include <string.h>
#include "queue.h"
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { fails++; printf("FAIL: %s\n", msg); } else printf("ok   %s\n", msg); } while (0)
static const char *cur(queue_t *q) { singer_t *s; qsong_t *x = queue_current(q, &s); static char b[300]; if (!x) return "(nada)"; snprintf(b, sizeof b, "%s:%s", s->name, x->info.title); return b; }
static void sing(queue_t *q) { qsong_t *x = queue_current(q, NULL); if (x) queue_complete(q, x->id); }
int main(void) {
  queue_t q; queue_init(&q);
  queue_add(&q, "Ana", "/a/Ana - A1.zip");
  queue_add(&q, "Ana", "/a/Ana - A2.zip");
  queue_add(&q, "Bia", "/a/Bia - B1.zip");
  queue_add(&q, "Caio", "/a/Caio - C1.zip");
  CHECK(!strcmp(cur(&q), "Ana:A1"), "rodada comeca pelo primeiro a pedir");
  sing(&q); CHECK(!strcmp(cur(&q), "Bia:B1"), "1 musica por rodada: passa para Bia");
  queue_add(&q, "Davi", "/a/Davi - D1.zip");
  sing(&q); CHECK(!strcmp(cur(&q), "Caio:C1"), "depois Caio");
  sing(&q); CHECK(!strcmp(cur(&q), "Davi:D1"), "cantor novo entra no fim da rodada atual");
  sing(&q); CHECK(!strcmp(cur(&q), "Ana:A2"), "volta para Ana na rodada seguinte");
  sing(&q); CHECK(!strcmp(cur(&q), "(nada)"), "sem musicas: nada");
  queue_add(&q, "caio", "/a/Caio - C2.zip");
  CHECK(q.count == 4, "nome sem diferenciar maiusculas nao cria cantor novo");
  CHECK(!strcmp(cur(&q), "caio:C2") || !strcmp(cur(&q), "Caio:C2"), "cantor sem musica e pulado ate quem tem");
  for (int i = 0; i < 5; i++) queue_add(&q, "Eva", "/a/Eva - E.zip");
  CHECK(queue_add(&q, "Eva", "/a/Eva - E6.zip") == QUEUE_ERR_SINGER_FULL, "limite de 5 por cantor");
  for (int i = 0; i < 20; i++) queue_add(&q, "PS5", "/a/X - P.zip");
  CHECK(q.singers[5].count == 20, "cantor PS5 sem limite de 5");
  CHECK(queue_find(&q, "eva", "/a/Eva - E.zip") == 0, "acha a música pelo caminho");
  CHECK(queue_find(&q, "Eva", "/a/nao-existe.zip") == -1, "música fora da fila");
  CHECK(queue_remove_path(&q, "Eva", "/a/Eva - E.zip") == 0 && q.singers[4].count == 4, "tira a música pelo caminho");
  {
    int p = queue_singer_index(&q, "PS5");
    queue_t *qq = &q;
    int first = qq->singers[p].songs[0].id, third = qq->singers[p].songs[2].id;
    CHECK(queue_move_song(qq, p, 0, 2) == 2 && qq->singers[p].songs[2].id == first &&
          qq->singers[p].songs[1].id == third, "move música para baixo na lista do cantor");
    CHECK(queue_move_song(qq, p, 2, 0) == 0 && qq->singers[p].songs[0].id == first, "move música para cima");
    CHECK(queue_move_song(qq, p, 0, -5) == 0, "não passa do topo");
  }
  CHECK(queue_add_for(&q, "Zé", "cel-1", "/a/Z - 1.zip") == QUEUE_OK, "celular cria cantor");
  CHECK(queue_add_for(&q, "zé", "cel-2", "/a/Z - 2.zip") == QUEUE_ERR_NAME_TAKEN, "outro celular não usa o mesmo nome");
  CHECK(queue_add_for(&q, "Zé", "cel-1", "/a/Z - 3.zip") == QUEUE_OK && queue_singer_for_client(&q, "cel-1")->count == 2, "mesmo celular soma no mesmo cantor");
  {
    int zid = queue_singer_for_client(&q, "cel-1")->songs[0].id;
    unsigned v = q.version;
    CHECK(queue_cancel(&q, zid, "cel-2") == QUEUE_ERR_NOT_ALLOWED, "não cancela música de outro celular");
    CHECK(queue_cancel(&q, zid, "cel-1") == QUEUE_OK && q.version != v, "cancela a própria música e muda a versão");
  }
  singer_t *s; queue_current(&q, &s); int cur_idx = q.current;
  queue_remove_singer(&q, 0);
  CHECK(q.current == cur_idx - 1, "remover cantor antes do atual mantem a vez");
  queue_free(&q);
  printf("%d falha(s)\n", fails);
  return fails != 0;
}
