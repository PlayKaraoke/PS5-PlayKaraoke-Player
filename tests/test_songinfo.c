#include <stdio.h>
#include <string.h>
#include "songinfo.h"
static int fails = 0;
static void check(const char *name, const char *code, const char *artist, const char *title) {
  songinfo_t s; songinfo_parse(name, &s);
  int ok = !strcmp(s.code, code) && !strcmp(s.artist, artist) && !strcmp(s.title, title);
  if (!ok) fails++;
  printf("%s %-50s -> [%s] [%s] [%s]\n", ok ? "ok  " : "FAIL", name, s.code, s.artist, s.title);
}
int main(void) {
  check("EJBg-0020 - Kansas - Play the Game Tonight (Acoustic).zip", "EJBg-0020", "Kansas", "Play the Game Tonight (Acoustic)");
  check("SC8123-05 - Adele - Hello.zip", "SC8123-05", "Adele", "Hello");
  check("SF001-01 - Hello.zip", "SF001-01", "", "Hello");
  check("Queen - Bohemian Rhapsody.mp4", "", "Queen", "Bohemian Rhapsody");
  check("Some Random File Name.zip", "", "", "Some Random File Name");
  check("Nome da Musica - Artista - Extra.zip", "", "Nome da Musica", "Artista - Extra");
  check("PLKM-1813 - Artista X - Titulo Y.mp4", "PLKM-1813", "Artista X", "Titulo Y");
  check("Artista 2000 - Musica.zip", "", "Artista 2000", "Musica");
  check("ZOOM 1234 - Artista - Música Ção.zip", "ZOOM 1234", "Artista", "Música Ção");
  check("KVD-12345 - A-ha - Take On Me.mp4", "KVD-12345", "A-ha", "Take On Me");
  printf("%d falha(s)\n", fails);
  return fails != 0;
}
