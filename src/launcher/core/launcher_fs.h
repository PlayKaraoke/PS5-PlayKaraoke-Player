/* PlayKaraoke PS5: substitui o fs.h do websrv (que depende do
 * libmicrohttpd). Só o leitor de arquivos é usado pelo hbldr.c;
 * implementado em src/launcher/launcher.c. */

#pragma once

#include <stddef.h>
#include <stdint.h>

uint8_t* fs_readfile(const char* path, size_t* size);
