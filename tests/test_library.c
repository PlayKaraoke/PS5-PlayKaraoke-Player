#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "library.h"
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { fails++; printf("FAIL: %s\n", msg); } else printf("ok   %s\n", msg); } while (0)
static void touch(const char *path) { FILE *f = fopen(path, "w"); if (f) fclose(f); }
int main(void) {
  char buf[256];
  library_fold("Ação Coração ÉÇÃ Müller Ñandú", buf, sizeof buf);
  CHECK(!strcmp(buf, "acao coracao eca muller nandu"), "tira acento e põe minúsculas");

  char dir[] = "/tmp/pk_lib_XXXXXX";
  if (!mkdtemp(dir)) return 1;
  char p[1024];
  snprintf(p, sizeof p, "%s/Produtora", dir); mkdir(p, 0755);
  snprintf(p, sizeof p, "%s/Produtora/SC8123-05 - Adele - Hello.zip", dir); touch(p);
  snprintf(p, sizeof p, "%s/Produtora/PLK-0001 - Fulano - Coração Valente.mp4", dir); touch(p);
  snprintf(p, sizeof p, "%s/Produtora/._PLK-0001 - Fulano - Coração Valente.mp4", dir); touch(p);
  snprintf(p, sizeof p, "%s/Produtora/leiame.txt", dir); touch(p);
  snprintf(p, sizeof p, "%s/.oculta", dir); mkdir(p, 0755);
  snprintf(p, sizeof p, "%s/.oculta/X - Y.zip", dir); touch(p);
  char index[1100];
  snprintf(index, sizeof index, "%s/index.txt", dir);

  library_t lib; library_init(&lib);
  const char *roots[] = {dir};
  library_scan_async(&lib, roots, 1, index);
  while (lib.scanning) usleep(1000);
  CHECK(library_count(&lib) == 2, "varre só .zip/.mp4, sem ocultos e fantasmas do macOS");

  char (*res)[1024] = malloc(LIBRARY_MAX_RESULTS * 1024);
  CHECK(library_search(&lib, "coracao", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, NULL) == 1, "busca sem acento acha com acento");
  CHECK(library_search(&lib, "FULANO valente", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, NULL) == 1, "todas as palavras, sem maiúsculas");
  CHECK(library_search(&lib, "sc8123", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, NULL) == 1, "busca pelo código");
  int total = 0;
  int n = library_search(&lib, "e", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, &total);
  CHECK(n == 2 && total == 2 && strstr(res[0], "Adele") && strstr(res[1], "Fulano"), "resultados em ordem alfabética de artista");
  CHECK(library_search(&lib, "e", LIBRARY_ONLY_MP4, res, LIBRARY_MAX_RESULTS, NULL) == 1 && strstr(res[0], ".mp4"), "filtro só MP4");
  CHECK(library_search(&lib, "e", LIBRARY_ONLY_CDG, res, LIBRARY_MAX_RESULTS, NULL) == 1 && strstr(res[0], ".zip"), "filtro só CDG");
  CHECK(library_search(&lib, "e", LIBRARY_ALL, res, 1, &total) == 1 && total == 2, "limite de resultados com total real");
  CHECK(library_search(&lib, "adele valente", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, NULL) == 0, "palavras de músicas diferentes não casam");
  library_free(&lib);

  library_t lib2; library_init(&lib2);
  CHECK(library_load(&lib2, index) == 2, "índice salvo e carregado de novo");
  CHECK(library_search(&lib2, "hello", LIBRARY_ALL, res, LIBRARY_MAX_RESULTS, NULL) == 1, "busca no índice carregado");
  library_free(&lib2);
  free(res);

  char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir); system(cmd);
  printf("%d falha(s)\n", fails);
  return fails != 0;
}
