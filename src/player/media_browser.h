#ifndef NDVIDEO_MEDIA_BROWSER_H
#define NDVIDEO_MEDIA_BROWSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void media_browser_configure(const char *);
void media_browser_shutdown(void);
const char *media_browser_root(void);
const char *media_browser_directory(void);
bool media_browser_at_root(void);
bool media_browser_enter(const char *, size_t, size_t);
bool media_browser_back(size_t *, size_t *);
void media_browser_to_root(void);
void media_browser_breadcrumb(char *, size_t);
const char *media_local_directory(const char *);
#endif
