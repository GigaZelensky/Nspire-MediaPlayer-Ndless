#ifndef NDVIDEO_DISPLAY_TEXT_H
#define NDVIDEO_DISPLAY_TEXT_H
#include <stdbool.h>
#include <stddef.h>
/* Output only. Never pass the folded name back to storage. USB FatFs names
 * use CP437; tags/local names use UTF-8, with a Latin-1 fallback. */
void display_text_ascii(const char *, char *, size_t, bool dos_names);
#endif
