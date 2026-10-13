# PlayKaraoke PS5 — build (roda dentro do container, ver scripts/ps5.sh).
#
#   build/app.elf          o app (tela, reprodução); roda como BigApp
#   build/playkaraoke.elf  o launcher, com o app e o ícone embutidos; é o
#                          arquivo injetado pelo elfldr

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK não definido: rode pelo scripts/ps5.sh)
endif

TITLE_ID := PLKR00001

APP := build/app.elf
APP_SRCS := src/app/main.c src/app/player.c src/app/cdg.c src/app/kzip.c src/app/osd.c src/app/pitch.c \
	src/app/playback.c src/app/ui.c src/app/assets.c src/app/songinfo.c src/app/queue.c src/app/browser.c src/app/library.c src/app/queue_view.c \
	src/app/server.c src/app/i18n.c vendor/qrcodegen/qrcodegen.c
APP_PKGS := sdl2 SDL2_gfx SDL2_ttf SDL2_image minizip libmicrohttpd libavformat libavcodec libswscale libswresample libavutil
# DEV=1 (padrão) liga as ferramentas de desenvolvimento (prints e controle
# simulado pela rede); a versão pública é feita com DEV=0.
DEV ?= 1
APP_CFLAGS := -Wall -Werror -O2 $(shell $(PKG_CONFIG) --cflags $(APP_PKGS)) $(if $(filter 1,$(DEV)),-DPK_DEV_TOOLS) \
	-DASSET_DIR='"$(CURDIR)/assets"' -DWWW_DIR='"$(CURDIR)/src/www"'
APP_LIBS := $(shell $(PKG_CONFIG) --libs $(APP_PKGS))

LAUNCHER := build/playkaraoke.elf
LAUNCHER_SRCS := src/launcher/launcher.c src/launcher/core/hbldr.c \
	src/launcher/core/elfldr.c src/launcher/core/pt.c
ICON := assets/icon0.png
LAUNCHER_CFLAGS := -Wall -Werror -O2 -Isrc/launcher \
	-DTITLE_ID='"$(TITLE_ID)"' -DHBLDR_TITLE_ID='"$(TITLE_ID)"' \
	-DAPP_ELF_FILE='"$(CURDIR)/$(APP)"' -DICON_FILE='"$(CURDIR)/$(ICON)"' \
	-DLOGO_SVG_FILE='"$(CURDIR)/assets/logo.svg"'
LAUNCHER_LIBS := -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil

all: $(LAUNCHER)

$(APP): $(APP_SRCS) $(wildcard src/app/*.h) $(wildcard assets/fonts/*.ttf) assets/logo.png src/www/index.html
	@mkdir -p $(@D)
	$(CC) $(APP_CFLAGS) -o $@ $(APP_SRCS) $(APP_LIBS)

$(LAUNCHER): $(LAUNCHER_SRCS) $(wildcard src/launcher/core/*.h) $(APP) $(ICON) assets/logo.svg
	$(CC) $(LAUNCHER_CFLAGS) -o $@ $(LAUNCHER_SRCS) $(LAUNCHER_LIBS)

clean:
	rm -rf build


.PHONY: all clean
