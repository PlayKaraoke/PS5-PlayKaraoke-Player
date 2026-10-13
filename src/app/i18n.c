/* PlayKaraoke PS5 — idioma do app. Ver i18n.h. */

#include "i18n.h"

#include <stdio.h>
#include <string.h>

lang_t g_lang = LANG_EN;

void i18n_load(void) {
  FILE *f = fopen(CONFIG_FILE, "r");
  if (!f) return;
  char line[128];
  while (fgets(line, sizeof line, f))
    if (strncmp(line, "lang=", 5) == 0) g_lang = strncmp(line + 5, "pt", 2) == 0 ? LANG_PT : LANG_EN;
  fclose(f);
}

void i18n_set(lang_t lang) {
  g_lang = lang;
  FILE *f = fopen(CONFIG_FILE, "w");
  if (!f) return;
  fprintf(f, "lang=%s\n", i18n_code());
  fclose(f);
}

const char *i18n_code(void) {
  return g_lang == LANG_PT ? "pt" : "en";
}
