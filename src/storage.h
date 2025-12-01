#ifndef _STORAGE_H
#define _STORAGE_H

#include <stddef.h>
#include <sys/types.h>

#define STORAGE_MAX_SSID_SIZE 32
#define STORAGE_MIN_PASS_SIZE 8
#define STORAGE_MAX_PASS_SIZE 64

ssize_t storage_ssid_get(char *buf, size_t len);
ssize_t storage_ssid_set(const char *buf, size_t len);

ssize_t storage_pass_get(char *buf, size_t len);
ssize_t storage_pass_set(const char *buf, size_t len);

#endif // _STORAGE_H
