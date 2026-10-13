/* PlayKaraoke PS5 — idioma do app (inglês, padrão, ou português).
 *
 * Cada texto fica escrito nos dois idiomas onde ele aparece:
 *     TR("Search", "Buscar")
 * A escolha é salva em CONFIG_FILE (também lida pelo launcher, para a
 * página do navegador) e enviada aos celulares no estado. */

#pragma once

#define CONFIG_FILE "/data/playkaraoke/config.txt"

typedef enum { LANG_EN = 0, LANG_PT = 1 } lang_t;

extern lang_t g_lang;

#define TR(en, pt) (g_lang == LANG_PT ? (pt) : (en))

/* Lê o idioma salvo (inglês se não houver). */
void i18n_load(void);

/* Troca o idioma e salva. */
void i18n_set(lang_t lang);

/* "en" ou "pt" */
const char *i18n_code(void);
