/* PlayKaraoke PS5 — helper minimalista para liberar acesso ao filesystem
 * (/data/playkaraoke/songs e /mnt/usb*) quando rodando como app nativo FSELF. */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>

static pid_t find_pid(void) {
  DIR *d = opendir("/mnt/sandbox");
  if (!d) return -1;
  struct dirent *e;
  pid_t pid = -1;
  while ((e = readdir(d))) {
    if (strncmp(e->d_name, "PLKR00001", 9) != 0) continue;
    char path[256];
    snprintf(path, sizeof path, "/mnt/sandbox/%s/download0/pk_pid", e->d_name);
    FILE *f = fopen(path, "r");
    if (!f) continue;
    long v = -1;
    if (fscanf(f, "%ld", &v) == 1 && v > 0) pid = (pid_t)v;
    fclose(f);
    unlink(path);
    if (pid > 0) break;
  }
  closedir(d);
  return pid;
}

int main(void) {
  pid_t pid = find_pid();
  if (pid <= 0) return 1;
  intptr_t root = kernel_get_root_vnode();
  kernel_set_proc_rootdir(pid, root);
  kernel_set_proc_jaildir(pid, root);
  kernel_set_ucred_prison(pid, KERNEL_ADDRESS_PRISON0);
  kernel_set_ucred_uid(pid, 0);
  kernel_set_ucred_ruid(pid, 0);
  kernel_set_ucred_svuid(pid, 0);
  kernel_set_ucred_rgid(pid, 0);
  kernel_set_ucred_svgid(pid, 0);
  uint8_t caps[16];
  memset(caps, 0xFF, sizeof caps);
  kernel_set_ucred_caps(pid, caps);
  kernel_set_ucred_authid(pid, 0x4801000000000013UL);
  return 0;
}
