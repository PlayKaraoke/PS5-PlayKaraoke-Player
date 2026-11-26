/* PlayKaraoke PS5 — camada de compatibilidade para o build nativo FSELF (eboot.bin). */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern int sceSysmoduleLoadModule(uint16_t id);
extern int sceNetInit(void);

#ifndef PK_HELPER_ELF
#error "PK_HELPER_ELF não definido"
#endif

__asm__(".section .rodata\n"
        ".global pk_helper_elf\npk_helper_elf:\n"
        ".incbin \"" PK_HELPER_ELF "\"\npk_helper_elf_end:\n"
        ".global pk_helper_elf_size\npk_helper_elf_size:\n"
        ".quad pk_helper_elf_end - pk_helper_elf\n"
        ".previous\n");
extern const uint8_t pk_helper_elf[];
extern const uint64_t pk_helper_elf_size;

static void pk_escape_sandbox(void) {
  if (access("/data/playkaraoke/songs", R_OK) == 0) return;

  (void)sceNetInit();

  FILE *f = fopen("/download0/pk_pid", "w");
  if (!f) return;
  fprintf(f, "%ld\n", (long)getpid());
  fclose(f);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9021);
    addr.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
      size_t off = 0;
      while (off < pk_helper_elf_size) {
        ssize_t n = send(fd, pk_helper_elf + off, pk_helper_elf_size - off, 0);
        if (n <= 0) break;
        off += (size_t)n;
      }
      shutdown(fd, SHUT_WR);
      char tmp[64];
      while (recv(fd, tmp, sizeof tmp, 0) > 0) {}
    }
    close(fd);
  }

  for (int i = 0; i < 30; i++) {
    if (access("/data/playkaraoke/songs", R_OK) == 0) break;
    usleep(50000);
  }
  (void)chdir("/");
}

__attribute__((constructor)) static void pk_native_init_modules(void) {
  sceSysmoduleLoadModule(0x0096); /* SCE_SYSMODULE_IME_DIALOG */
  pk_escape_sandbox();
}

int sceKeyboardInit(void) { return -1; }
int sceKeyboardOpen(int user_id, int type, int index, void *param) {
  (void)user_id; (void)type; (void)index; (void)param; return -1;
}
int sceKeyboardReadState(int handle, void *state) {
  (void)handle; (void)state; return -1;
}

int statfs(const char *path, struct statfs *buf) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  memset(buf, 0, sizeof *buf);
  buf->f_bsize = 4096;
  buf->f_blocks = 10000000;
  return 0;
}

char *strcasestr(const char *haystack, const char *needle) {
  if (!*needle) return (char *)haystack;
  size_t nlen = strlen(needle);
  for (; *haystack; haystack++) {
    size_t i = 0;
    while (i < nlen && tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i])) i++;
    if (i == nlen) return (char *)haystack;
  }
  return NULL;
}

void arc4random_buf(void *buf, size_t nbytes) {
  unsigned char *p = buf;
  uint32_t s = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)buf;
  for (size_t i = 0; i < nbytes; i++) {
    s = s * 1664525u + 1013904223u;
    p[i] = (unsigned char)(s >> 24);
  }
}

int isatty(int fd) { (void)fd; return 0; }

int mkstemp(char *tmpl) {
  size_t len = strlen(tmpl);
  if (len < 6) { errno = EINVAL; return -1; }
  uint32_t s = (uint32_t)time(NULL);
  for (int tries = 0; tries < 64; tries++) {
    for (int i = 0; i < 6; i++) {
      s = s * 1664525u + 1013904223u;
      tmpl[len - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[(s >> 24) % 36];
    }
    int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) return fd;
  }
  return -1;
}

int getaddrinfo(const char *n, const char *s, const void *h, void **r) {
  (void)n; (void)s; (void)h; if (r) *r = NULL; return -1;
}
void freeaddrinfo(void *r) { (void)r; }
int getnameinfo(const void *sa, unsigned sl, char *h, unsigned hl, char *s, unsigned svl, int f) {
  (void)sa; (void)sl; (void)h; (void)hl; (void)s; (void)svl; (void)f; return -1;
}
const char *gai_strerror(int e) { (void)e; return "disabled"; }

void __assert(const char *func, const char *file, int line, const char *expr) {
  (void)func; (void)file; (void)line; (void)expr;
  abort();
}

struct tm *localtime_r(const time_t *t, struct tm *res) {
  struct tm *p = localtime(t);
  if (!p) return NULL;
  *res = *p;
  return res;
}

struct tm *gmtime_r(const time_t *t, struct tm *res) {
  struct tm *p = gmtime(t);
  if (!p) return NULL;
  *res = *p;
  return res;
}

int pipe2(int fds[2], int flags) {
  if (pipe(fds) != 0) return -1;
  if (flags & O_CLOEXEC) {
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  }
  if (flags & O_NONBLOCK) {
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
  }
  return 0;
}

void *tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *)) {
  (void)key; (void)rootp; (void)compar; return NULL;
}
void *tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *)) {
  (void)key; (void)rootp; (void)compar; return NULL;
}
void *tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *)) {
  (void)key; (void)rootp; (void)compar; return NULL;
}

char *nl_langinfo(int item) { (void)item; return "UTF-8"; }
int ___mb_cur_max(void) { return 4; }
int dladdr(const void *addr, void *info) { (void)addr; (void)info; return 0; }
int _setjmp(jmp_buf env) { return setjmp(env); }
void _longjmp(jmp_buf env, int val) { longjmp(env, val); }
