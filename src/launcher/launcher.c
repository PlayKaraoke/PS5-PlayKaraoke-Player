/* PlayKaraoke PS5 — launcher residente (é o playkaraoke.elf injetado).
 *
 * Ao ser injetado pelo elfldr:
 *   1. encerra um launcher anterior (reinjeção = atualização);
 *   2. grava o app embutido em /data/playkaraoke/app.elf;
 *   3. instala/atualiza o ícone na seção Mídia (/user/app/<TITLE_ID>);
 *   4. fica residente ouvindo só em 127.0.0.1:LAUNCHER_PORT. O ícone abre
 *      http://127.0.0.1:LAUNCHER_PORT/launch (deeplinkUri do param.json) e o
 *      launcher abre o app como BigApp pelo núcleo do websrv (core/hbldr.c),
 *      o único tipo de processo com acesso à tela.
 *
 * Logs: /data/playkaraoke/logs/launcher.log e app.log (stdout do app; a
 * execução anterior fica em app.prev.log). */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "core/hbldr.h"
#include "core/launcher_fs.h"

#ifndef TITLE_ID
#error "TITLE_ID não definido"
#endif
#ifndef APP_ELF_FILE
#error "APP_ELF_FILE não definido"
#endif
#ifndef ICON_FILE
#error "ICON_FILE não definido"
#endif
#ifndef LOGO_SVG_FILE
#error "LOGO_SVG_FILE não definido"
#endif

#define LAUNCHER_PORT 9050
#define LAUNCHER_NAME "pk-launcher.elf"

#define DATA_DIR "/data/playkaraoke"
#define LOG_DIR DATA_DIR "/logs"
#define APP_PATH DATA_DIR "/app.elf"
#define TILE_DIR "/user/app/" TITLE_ID

#define APPINST_AUTHID 0x4801000000000013L

/* Arquivo embutido no .elf (app e ícone): símbolos name e name_size. */
#define EMBED(name, file)                                     \
  __asm__(".section .rodata\n"                                \
          ".global " #name "\n" #name ":\n"                   \
          ".incbin \"" file "\"\n" #name "_end:\n"            \
          ".global " #name "_size\n" #name "_size:\n"         \
          ".quad " #name "_end - " #name "\n"                 \
          ".previous\n");                                     \
  extern const uint8_t name[];                                \
  extern const uint64_t name##_size

EMBED(embedded_app, APP_ELF_FILE);
EMBED(embedded_icon, ICON_FILE);
EMBED(embedded_logo_svg, LOGO_SVG_FILE);

/* Ícone na seção Mídia (65536). Abrir o ícone só chama o deeplinkUri. */
static const char tile_param_json[] =
  "{\n"
  "  \"applicationCategoryType\": 65536,\n"
  "  \"attribute\": 536870913,\n"
  "  \"attribute2\": 0,\n"
  "  \"attribute3\": 4,\n"
  "  \"titleId\": \"" TITLE_ID "\",\n"
  "  \"deeplinkUri\": \"http://127.0.0.1:9050/launch\",\n"
  "  \"localizedParameters\": {\n"
  "    \"defaultLanguage\": \"en-US\",\n"
  "    \"en-US\": {\n"
  "      \"titleName\": \"PlayKaraoke\"\n"
  "    }\n"
  "  }\n"
  "}\n";

/* Página que o navegador do PS5 mostra ao abrir o ícone (ver serve()). Ele
 * fica por cima do app e só fecha com ○, então a página serve de manual
 * rápido: logo, comandos e avisos. O logo (SVG) é embutido no meio. */
#define LAUNCH_DELAY_US 800000

/* Medidas em vh (altura da tela): a página cabe inteira qualquer que seja a
 * resolução do navegador do PS5. */
static const char page_head[] =
  "<!doctype html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width\"><style>"
  "html,body{margin:0;height:100%;overflow:hidden}"
  "body{background:#0f0c17;color:#fff;font-family:sans-serif}"
  ".wrap{padding:3.5vh 6vw}"
  ".logo svg{height:12vh;width:auto}"
  ".box{background:#2e2340;border-radius:2vh;padding:1.6vh 2.4vh;font-size:2.7vh;margin:2.4vh 0 .4vh}"
  ".warn{background:#5a1630}"
  "h2{color:#c040f0;font-size:2vh;letter-spacing:.1em;text-transform:uppercase;margin:2.4vh 0 1vh}"
  ".grid{display:grid;grid-template-columns:1fr 1fr;gap:1vh 4vw;font-size:2.35vh;color:#ddd6ea}"
  ".b{display:inline-block;width:3.9vh;height:3.9vh;border-radius:50%;background:#2a2436;"
  "text-align:center;line-height:3.9vh;font-weight:bold;margin-right:1.3vh;vertical-align:middle}"
  ".x{color:#7cb2e8}.o{color:#ff666e;font-size:2.8vh}.t{color:#40e2a0}.s{color:#ff69f8;font-size:2.8vh}"
  ".p{display:inline-block;background:#3a3348;border-radius:1.3vh;padding:.3vh 1.3vh;"
  "font-size:1.95vh;margin-right:1.3vh;color:#fff;vertical-align:middle}"
  ".note{font-size:2.2vh;color:#b4adc2;line-height:1.45}"
  "</style></head><body><div class=\"wrap\"><div class=\"logo\">";

static const char launch_body_en[] =
  "</div>"
  "<div class=\"box\"><span class=\"b o\">&#9675;</span>This window is the PS5 web browser. "
  "Press <b>&#9675;</b> to close it and use PlayKaraoke.</div>"
  "<h2>Home and search</h2><div class=\"grid\">"
  "<div><span class=\"b t\">&#9651;</span>Search songs</div>"
  "<div><span class=\"b x\">&#10005;</span>Add to / remove from the queue</div>"
  "<div><span class=\"b s\">&#9633;</span>Start the show</div>"
  "<div><span class=\"p\">OPTIONS</span>See the queue (any time)</div>"
  "<div><span class=\"p\">R1</span>Filter: CDG, MP4 or both &middot; language on the home screen</div>"
  "<div><span class=\"b o\">&#9675;</span>Back</div></div>"
  "<h2>While a song is playing</h2><div class=\"grid\">"
  "<div><span class=\"b x\">&#10005;</span>Pause and resume</div>"
  "<div><span class=\"b o\">&#9675;</span>End the song</div>"
  "<div><span class=\"p\">&uarr; &darr;</span>Volume (D-pad or left stick)</div>"
  "<div><span class=\"p\">L1 R1</span>Key &minus;1 / +1</div>"
  "<div><span class=\"p\">R &larr; &rarr;</span>Back / forward 10 s (right stick)</div>"
  "<div><span class=\"p\">OPTIONS</span>See the queue without stopping the song</div></div>"
  "<h2>Before you start</h2><div class=\"note\">"
  "Connect the drive with your karaoke files, formatted as <b>exFAT</b> (or FAT32), <b>before</b> opening "
  "the app. Formats: <b>.zip</b> (CDG + MP3) and <b>.mp4</b>, named "
  "&ldquo;Code - Artist - Song&rdquo;.<br>"
  "Guests request songs on their phones by scanning the QR code on the TV (same Wi-Fi).<br>"
  "Close any open game before opening PlayKaraoke: it never closes anything by itself."
  "</div></div></body></html>";

static const char launch_body_pt[] =
  "</div>"
  "<div class=\"box\"><span class=\"b o\">&#9675;</span>Esta janela &eacute; do navegador do PS5. "
  "Aperte <b>&#9675;</b> para fech&aacute;-la e usar o PlayKaraoke.</div>"
  "<h2>Tela inicial e busca</h2><div class=\"grid\">"
  "<div><span class=\"b t\">&#9651;</span>Buscar m&uacute;sica</div>"
  "<div><span class=\"b x\">&#10005;</span>Adicionar ou tirar da fila</div>"
  "<div><span class=\"b s\">&#9633;</span>Iniciar show</div>"
  "<div><span class=\"p\">OPTIONS</span>Ver a fila (a qualquer momento)</div>"
  "<div><span class=\"p\">R1</span>Filtro: CDG, MP4 ou os dois &middot; idioma na tela inicial</div>"
  "<div><span class=\"b o\">&#9675;</span>Voltar</div></div>"
  "<h2>Durante a m&uacute;sica</h2><div class=\"grid\">"
  "<div><span class=\"b x\">&#10005;</span>Pausar e continuar</div>"
  "<div><span class=\"b o\">&#9675;</span>Encerrar a m&uacute;sica</div>"
  "<div><span class=\"p\">&uarr; &darr;</span>Volume (setas ou anal&oacute;gico esquerdo)</div>"
  "<div><span class=\"p\">L1 R1</span>Tom &minus;1 / +1</div>"
  "<div><span class=\"p\">R &larr; &rarr;</span>Voltar / avan&ccedil;ar 10 s (anal&oacute;gico direito)</div>"
  "<div><span class=\"p\">OPTIONS</span>Ver a fila sem parar a m&uacute;sica</div></div>"
  "<h2>Antes de come&ccedil;ar</h2><div class=\"note\">"
  "Conecte o HD com os karaok&ecirc;s, formatado em <b>exFAT</b> (ou FAT32), <b>antes</b> de abrir o app. "
  "Formatos: <b>.zip</b> (CDG + MP3) e <b>.mp4</b>, com o nome no padr&atilde;o "
  "&ldquo;C&oacute;digo - Artista - M&uacute;sica&rdquo;.<br>"
  "Os convidados pedem m&uacute;sicas pelo celular, lendo o QR code da TV (mesmo Wi-Fi).<br>"
  "Feche jogos abertos antes de abrir o PlayKaraoke: ele n&atilde;o fecha nada sozinho."
  "</div></div></body></html>";

/* Quando já há um jogo/app aberto: o launcher não fecha nada. */
static const char busy_body_en[] =
  "</div>"
  "<div class=\"box warn\">A game or app is open. Close it (PS button &rarr; Close) "
  "and open PlayKaraoke again.</div>"
  "<div class=\"box\"><span class=\"b o\">&#9675;</span>Press <b>&#9675;</b> to close this window.</div>"
  "</div></body></html>";

static const char busy_body_pt[] =
  "</div>"
  "<div class=\"box warn\">Tem um jogo ou app aberto. Feche-o (bot&atilde;o PS &rarr; Fechar) "
  "e abra o PlayKaraoke de novo.</div>"
  "<div class=\"box\"><span class=\"b o\">&#9675;</span>Aperte <b>&#9675;</b> para fechar esta janela.</div>"
  "</div></body></html>";

/* Idioma escolhido no app (config.txt, "lang=pt" ou "lang=en"). */
static int lang_is_pt(void) {
  FILE *f = fopen(DATA_DIR "/config.txt", "r");
  if (!f) return 0;
  char line[64];
  int pt = 0;
  while (fgets(line, sizeof line, f))
    if (strncmp(line, "lang=pt", 7) == 0) pt = 1;
  fclose(f);
  return pt;
}

typedef struct notify_request {
  char reserved[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceUserServiceInitialize(void *);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void *);
int sceSystemServiceGetAppIdOfRunningBigApp(void);

static FILE *log_file;

static void log_msg(const char *fmt, ...) {
  char stamp[32];
  time_t now = time(NULL);
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));

  va_list ap;
  va_start(ap, fmt);
  if (log_file) {
    fprintf(log_file, "%s ", stamp);
    vfprintf(log_file, fmt, ap);
    fputc('\n', log_file);
    fflush(log_file);
  }
  va_end(ap);

  va_start(ap, fmt);
  printf("[launcher] ");
  vprintf(fmt, ap);
  putchar('\n');
  fflush(stdout);
  va_end(ap);
}

static void notify(const char *text) {
  notify_request_t req;
  memset(&req, 0, sizeof req);
  strncpy(req.message, text, sizeof req.message - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

static int make_dir(const char *path) {
  if (mkdir(path, 0755) != 0 && errno != EEXIST) {
    log_msg("mkdir %s: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

/* Grava via arquivo temporário + rename: nunca deixa um arquivo pela metade. */
static int write_file(const char *path, const void *data, size_t size, mode_t mode) {
  char tmp[512];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) {
    log_msg("abrir %s: %s", tmp, strerror(errno));
    return -1;
  }
  const uint8_t *p = data;
  size_t left = size;
  while (left > 0) {
    ssize_t n = write(fd, p, left);
    if (n <= 0) {
      log_msg("gravar %s: %s", tmp, strerror(errno));
      close(fd);
      unlink(tmp);
      return -1;
    }
    p += n;
    left -= (size_t)n;
  }
  close(fd);
  if (rename(tmp, path) != 0) {
    log_msg("renomear %s: %s", path, strerror(errno));
    unlink(tmp);
    return -1;
  }
  return 0;
}

/* Usado pelo core/hbldr.c. Devolve um buffer alocado com malloc. */
uint8_t *fs_readfile(const char *path, size_t *size) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return NULL;

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return NULL;
  }
  uint8_t *buf = malloc((size_t)st.st_size);
  if (!buf) {
    close(fd);
    return NULL;
  }
  size_t have = 0;
  while (have < (size_t)st.st_size) {
    ssize_t n = read(fd, buf + have, (size_t)st.st_size - have);
    if (n <= 0) {
      free(buf);
      close(fd);
      return NULL;
    }
    have += (size_t)n;
  }
  close(fd);
  if (size) *size = have;
  return buf;
}

/* PID de outro processo com o nome dado (kinfo_proc via sysctl). */
static pid_t find_other_pid(const char *name) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t size = 0;
  if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0) return -1;
  uint8_t *buf = malloc(size);
  if (!buf) return -1;
  if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
    free(buf);
    return -1;
  }
  pid_t found = -1;
  pid_t me = getpid();
  for (uint8_t *p = buf; p < buf + size;) {
    int structsize = *(int *)p;
    pid_t pid = *(pid_t *)&p[72];
    const char *tdname = (const char *)&p[447];
    if (structsize <= 0) break;
    if (pid != me && strcmp(tdname, name) == 0) found = pid;
    p += structsize;
  }
  free(buf);
  return found;
}

static void stop_previous_launcher(void) {
  pid_t pid;
  while ((pid = find_other_pid(LAUNCHER_NAME)) > 0) {
    log_msg("encerrando launcher anterior (pid %d)", (int)pid);
    if (kill(pid, SIGKILL) != 0) break;
    sleep(1);
  }
}

static int install_tile(void) {
  if (make_dir(TILE_DIR) || make_dir(TILE_DIR "/sce_sys")) return -1;
  if (write_file(TILE_DIR "/sce_sys/param.json", tile_param_json, sizeof tile_param_json - 1, 0644) ||
      write_file(TILE_DIR "/sce_sys/icon0.png", embedded_icon, embedded_icon_size, 0644))
    return -1;

  /* AppInstUtil só aceita processos com o authid do instalador. */
  pid_t me = getpid();
  uint64_t authid = kernel_get_ucred_authid(me);
  kernel_set_ucred_authid(me, APPINST_AUTHID);

  int rc = -1;
  if (sceAppInstUtilInitialize() == 0) {
    int (*install_title_dir)(const char *, const char *, void *) = NULL;
    uint32_t handle;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
      install_title_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    rc = install_title_dir ? install_title_dir(TITLE_ID, "/user/app/", NULL)
                           : sceAppInstUtilAppInstallAll(NULL);
  }

  kernel_set_ucred_authid(me, authid);
  log_msg("icone %s registrado (rc=0x%08x)", TITLE_ID, (unsigned)rc);
  return rc == 0 ? 0 : -1;
}

static int open_listener(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(LAUNCHER_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  /* O launcher anterior pode levar um instante para soltar a porta. */
  for (int tries = 0; bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0; tries++) {
    if (tries == 10) {
      log_msg("bind na porta %d: %s", LAUNCHER_PORT, strerror(errno));
      close(fd);
      return -1;
    }
    sleep(1);
  }
  if (listen(fd, 4) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int launch_app(void) {
  rename(LOG_DIR "/app.log", LOG_DIR "/app.prev.log");
  int out = open(LOG_DIR "/app.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  char *argv[] = {NULL};
  char *envp[] = {NULL};

  log_msg("abrindo o app");
  pid_t pid = hbldr_launch(DATA_DIR, APP_PATH, out, argv, envp);
  if (out >= 0) close(out);
  log_msg(pid > 0 ? "app aberto (pid %d)" : "falha ao abrir o app (%d)", (int)pid);
  return pid > 0 ? 0 : -1;
}

/* Página = cabeçalho + logo SVG + corpo. */
static void send_all(int fd, const void *data, size_t size) {
  const char *p = data;
  while (size > 0) {
    ssize_t n = send(fd, p, size, 0);
    if (n <= 0) return;
    p += n;
    size -= (size_t)n;
  }
}

static void send_page(int client, const char *body) {
  size_t length = strlen(page_head) + embedded_logo_svg_size + strlen(body);
  char header[256];
  int len = snprintf(header, sizeof header,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %zu\r\nConnection: close\r\n\r\n", length);
  send_all(client, header, (size_t)len);
  send_all(client, page_head, strlen(page_head));
  send_all(client, embedded_logo_svg, embedded_logo_svg_size);
  send_all(client, body, strlen(body));
}

static void serve(int listener) {
  for (;;) {
    int client = accept(listener, NULL, NULL);
    if (client < 0) {
      if (errno == EINTR) continue;
      log_msg("accept: %s", strerror(errno));
      sleep(1);
      continue;
    }

    char req[1024];
    ssize_t n = recv(client, req, sizeof req - 1, 0);
    req[n > 0 ? n : 0] = '\0';

    if (strncmp(req, "GET /launch ", 12) != 0) {
      static const char not_found[] =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      send(client, not_found, sizeof not_found - 1, 0);
      close(client);
      continue;
    }

    /* Nunca fecha um jogo/app aberto (o hbldr encerraria o BigApp atual e
     * o jogo perderia o que não estava salvo, ou até corromperia o save). */
    int running = sceSystemServiceGetAppIdOfRunningBigApp();

    /* Responde antes de abrir o app, para ele subir por último (na frente). */
    int pt = lang_is_pt();
    send_page(client, running > 0 ? (pt ? busy_body_pt : busy_body_en) : (pt ? launch_body_pt : launch_body_en));
    close(client);

    if (running > 0) {
      log_msg("ja existe um app aberto (id 0x%x): nao abro o PlayKaraoke", (unsigned)running);
      notify("Feche o jogo ou app aberto antes de abrir o PlayKaraoke");
      continue;
    }
    usleep(LAUNCH_DELAY_US);
    launch_app();
  }
}

int main(void) {
  syscall(SYS_thr_set_name, -1, LAUNCHER_NAME);

  if (make_dir(DATA_DIR) || make_dir(LOG_DIR)) return 1;
  rename(LOG_DIR "/launcher.log", LOG_DIR "/launcher.prev.log");
  log_file = fopen(LOG_DIR "/launcher.log", "w");
  log_msg("iniciando (titulo %s, app %lu bytes)", TITLE_ID, (unsigned long)embedded_app_size);

  if (sceUserServiceInitialize(NULL) != 0) log_msg("sceUserServiceInitialize falhou");

  stop_previous_launcher();

  if (write_file(APP_PATH, embedded_app, embedded_app_size, 0755) != 0) {
    notify("PlayKaraoke: falha ao instalar (veja logs)");
    return 1;
  }
  log_msg("app gravado em %s", APP_PATH);

  if (install_tile() != 0) log_msg("aviso: registro do icone falhou");

  int listener = open_listener();
  if (listener < 0) {
    notify("PlayKaraoke: falha ao iniciar o launcher (veja logs)");
    return 1;
  }

  log_msg("pronto: abra o PlayKaraoke em Midia");
  notify("PlayKaraoke pronto: abra pelo icone em Midia");

  /* Solta a conexão do elfldr: daqui em diante a saída vai só para o log. */
  int devnull = open("/dev/null", O_RDWR);
  if (devnull >= 0) dup2(devnull, STDIN_FILENO);
  if (log_file) {
    dup2(fileno(log_file), STDOUT_FILENO);
    dup2(fileno(log_file), STDERR_FILENO);
  }

  serve(listener);
  return 0;
}
