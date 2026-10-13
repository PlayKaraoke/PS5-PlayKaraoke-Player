/* PlayKaraoke PS5 — arquivos embutidos no app. ASSET_DIR vem do Makefile.
 * Fontes: Inter e Montserrat, SIL Open Font License 1.1 (assets/fonts). */

#include "assets.h"

#ifndef ASSET_DIR
#error "ASSET_DIR não definido"
#endif
#ifndef WWW_DIR
#error "WWW_DIR não definido"
#endif

/* Arquivo embutido: símbolos name e name_size. */
#define EMBED(name, file)                             \
  __asm__(".section .rodata\n"                        \
          ".global " #name "\n" #name ":\n"           \
          ".incbin \"" file "\"\n" #name "_end:\n"    \
          ".global " #name "_size\n" #name "_size:\n" \
          ".quad " #name "_end - " #name "\n"         \
          ".previous\n")

EMBED(font_inter_regular, ASSET_DIR "/fonts/Inter-Regular.ttf");
EMBED(font_inter_semibold, ASSET_DIR "/fonts/Inter-SemiBold.ttf");
EMBED(font_montserrat_bold, ASSET_DIR "/fonts/Montserrat-Bold.ttf");
EMBED(logo_png, ASSET_DIR "/logo.png");
EMBED(www_index_html, WWW_DIR "/index.html");
