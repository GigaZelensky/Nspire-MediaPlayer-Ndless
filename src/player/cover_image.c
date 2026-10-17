#include "cover_image.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
/* A tag may claim a small JPEG while expanding to an enormous bitmap. Limit
 * dimensions and all temporary allocations, including progressive JPEG/PNG. */
enum
{
    COVER_HEAP_LIMIT = 8 * 1024 * 1024
};
typedef union
{
    size_t bytes;
    uint64_t alignment;
} CoverAllocation;
static size_t cover_heap;
static void *cover_malloc(size_t bytes)
{
    if (bytes > COVER_HEAP_LIMIT - cover_heap)
        return NULL;
    CoverAllocation *p = malloc(sizeof(*p) + bytes);
    if (!p)
        return NULL;
    p->bytes = bytes;
    cover_heap += bytes;
    return p + 1;
}
static void cover_free(void *ptr)
{
    if (ptr)
    {
        CoverAllocation *p = (CoverAllocation *)ptr - 1;
        cover_heap -= p->bytes;
        free(p);
    }
}
static void *cover_realloc(void *ptr, size_t bytes)
{
    if (!ptr)
        return cover_malloc(bytes);
    CoverAllocation *p = (CoverAllocation *)ptr - 1;
    size_t old = p->bytes;
    if (bytes > old && bytes - old > COVER_HEAP_LIMIT - cover_heap)
        return NULL;
    p = realloc(p, sizeof(*p) + bytes);
    if (!p)
        return NULL;
    p->bytes = bytes;
    cover_heap = cover_heap - old + bytes;
    return p + 1;
}
typedef struct
{
    uint16_t *output;
    unsigned side, width, height, dw, dh, ox, oy, next_y;
    uint32_t sums[240][3];
} CoverRows;
static CoverRows *jpeg_rows;
static void cover_row(CoverRows *rows, const uint8_t *rgb, unsigned y)
{
    while (rows->next_y < rows->dh)
    {
        unsigned y0 = rows->next_y * rows->height / rows->dh;
        unsigned y1 = (rows->next_y + 1) * rows->height / rows->dh;
        if (y1 == y0)
            ++y1;
        if (y < y0 || y >= y1)
            break;
        for (unsigned x = 0; x < rows->dw; ++x)
        {
            unsigned x0 = x * rows->width / rows->dw, x1 = (x + 1) * rows->width / rows->dw;
            if (x1 == x0)
                ++x1;
            for (unsigned sx = x0; sx < x1; ++sx)
                for (unsigned c = 0; c < 3; ++c)
                    rows->sums[x][c] += rgb[3 * sx + c];
            if (y + 1 == y1)
            {
                unsigned n = (x1 - x0) * (y1 - y0);
                uint32_t *sum = rows->sums[x];
                rows->output[(rows->oy + rows->next_y) * rows->side + rows->ox + x] =
                    (uint16_t)(((sum[0] / n) >> 3) << 11 | ((sum[1] / n) >> 2) << 5 |
                               ((sum[2] / n) >> 3));
                sum[0] = sum[1] = sum[2] = 0;
            }
        }
        if (y + 1 < y1)
            break;
        ++rows->next_y;
    }
}
static void jpeg_output_row(const uint8_t *rgb, int channels, unsigned width, unsigned y)
{
    (void)width;
    if (jpeg_rows && channels == 3)
        cover_row(jpeg_rows, rgb, y);
}
/* The optional local stb hook skips a full-resolution RGB allocation. JPEG
 * coefficients/components still use the bounded allocator above. This sink is
 * private to the foreground cover loader; it never runs during audio playback. */
#define STBI_JPEG_OUTPUT_ROW(rgb, n, w, y) jpeg_output_row(rgb, n, w, y)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 2048
#define STBI_MALLOC(n) cover_malloc(n)
#define STBI_REALLOC(p, n) cover_realloc(p, n)
#define STBI_FREE(p) cover_free(p)
#include "vendor/stb_image.h"

uint16_t *cover_image_decode(const void *bytes, size_t count, unsigned side)
{
    int w, h, c;
    if (!bytes || count > 512 * 1024 || !side || side > 240 ||
        !stbi_info_from_memory(bytes, (int)count, &w, &h, &c) || w <= 0 || h <= 0 ||
        (unsigned)w * (unsigned)h > 1536U * 1536U)
        return NULL;
    CoverRows rows = {.side = side, .width = (unsigned)w, .height = (unsigned)h};
    rows.dw = w >= h ? side : (unsigned)w * side / h;
    rows.dh = h >= w ? side : (unsigned)h * side / w;
    if (!rows.dw)
        rows.dw = 1;
    if (!rows.dh)
        rows.dh = 1;
    rows.ox = (side - rows.dw) / 2;
    rows.oy = (side - rows.dh) / 2;
    rows.output = calloc(side * side, sizeof(*rows.output));
    if (!rows.output)
        return NULL;
    bool jpeg = ((const uint8_t *)bytes)[0] == 255;
    jpeg_rows = jpeg ? &rows : NULL;
    uint8_t *rgb = stbi_load_from_memory(bytes, (int)count, &w, &h, &c, 3);
    jpeg_rows = NULL;
    if (!rgb)
    {
        free(rows.output);
        return NULL;
    }
    if (!jpeg)
        for (unsigned y = 0; y < (unsigned)h; ++y)
            cover_row(&rows, rgb + (size_t)y * w * 3, y);
    stbi_image_free(rgb);
    if (rows.next_y != rows.dh)
    {
        free(rows.output);
        return NULL;
    }
    return rows.output;
}
