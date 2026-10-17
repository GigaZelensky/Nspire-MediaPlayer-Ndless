#include "media_browser.h"
#include "display_text.h"
#include "../storage/usb/usb_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BYTES 512
typedef struct BrowserParent {
    struct BrowserParent *previous;
    size_t selected, scroll;
    char path[];
} BrowserParent;
static char root[PATH_BYTES] = ".", directory[PATH_BYTES] = ".";
static BrowserParent *parent;
void media_browser_shutdown(void)
{
    while (parent) {
        BrowserParent *old = parent;
        parent = parent->previous;
        free(old);
    }
}
void media_browser_configure(const char *path)
{
    media_browser_shutdown();
    snprintf(root, sizeof(root), "%s", path && *path ? path : ".");
    snprintf(directory, sizeof(directory), "%s", root);
}
const char *media_browser_root(void)
{
    return root;
}
const char *media_browser_directory(void)
{
    return directory;
}
bool media_browser_at_root(void)
{
    return !strcmp(root, directory);
}
bool media_browser_enter(const char *path, size_t selected, size_t scroll)
{
    if (!path || !*path || strlen(path) >= sizeof(directory) || !strcmp(path, directory))
        return false;
    BrowserParent *next = malloc(sizeof(*next) + strlen(directory) + 1);
    if (!next)
        return false;
    next->previous = parent;
    next->selected = selected;
    next->scroll = scroll;
    strcpy(next->path, directory);
    parent = next;
    strcpy(directory, path);
    return true;
}
bool media_browser_back(size_t *selected, size_t *scroll)
{
    if (!parent)
        return false;
    BrowserParent *old = parent;
    parent = old->previous;
    strcpy(directory, old->path);
    if (selected)
        *selected = old->selected;
    if (scroll)
        *scroll = old->scroll;
    free(old);
    return true;
}
void media_browser_to_root(void)
{
    media_browser_shutdown();
    strcpy(directory, root);
}
const char *media_local_directory(const char *path)
{
    return usb_path(path) ? root : path;
}
static void breadcrumb_raw(char *text,size_t bytes)
{
    if (media_browser_at_root()) {
        snprintf(text, bytes, "Media");
        return;
    }
    const char *relative = directory;
    const char *label = "Calculator";
    if (usb_path(directory)) {
        label = "USB";
        relative = strchr(directory + 5, '/');
        if (!relative)
            relative = "";
    } else if (!strncmp(directory, root, strlen(root)))
        relative = directory + strlen(root);
    if (*relative == '/')
        ++relative;
    if (!bytes)
        return;
    size_t used = (size_t)snprintf(text, bytes, "%s", label);
    if (used >= bytes)
        return;
    while (*relative && used + 1 < bytes) {
        if (used + 3 >= bytes)
            break;
        memcpy(text + used, " / ", 3);
        used += 3;
        while (*relative && *relative != '/' && used + 1 < bytes)
            text[used++] = *relative++;
        if (*relative == '/')
            ++relative;
    }
    text[used] = 0;
}

void media_browser_breadcrumb(char *text,size_t bytes)
{
    char raw[PATH_BYTES];breadcrumb_raw(raw,sizeof(raw));
    display_text_ascii(raw,text,bytes,usb_path(directory));
}
