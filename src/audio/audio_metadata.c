#include "audio_metadata.h"
#include "player/display_text.h"
#include <stdlib.h>
#include <string.h>

enum
{
    TAG_LIMIT = 1024 * 1024,
    TAG_READ_BUDGET = 2 * 1024 * 1024
};
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static uint32_t sync32(const uint8_t *p)
{
    return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 |
           (uint32_t)(p[2] & 127) << 7 | (p[3] & 127);
}
typedef struct
{
    AudioInput *in;
    size_t left;
    unsigned nodes;
} Reader;
static bool read_at(Reader *r, uint64_t at, void *out, size_t n)
{
    if (at > r->in->bytes || n > r->in->bytes - at || n > r->left)
        return false;
    r->left -= n;
    return r->in->seek(r->in->context, at) && r->in->read(r->in->context, out, n) == n;
}
static uint8_t *blob(Reader *r, uint64_t at, size_t n)
{
    if (!n || n > TAG_LIMIT || n > r->left)
        return NULL;
    uint8_t *p = malloc(n);
    if (p && !read_at(r, at, p, n))
    {
        free(p);
        p = NULL;
    }
    return p;
}
static void utf8_put(char *out, size_t size, size_t *used, uint32_t c)
{
    if (!c || c < 32)
        c = ' ';
    unsigned n = c < 128 ? 1 : c < 2048 ? 2 : 3;
    if (*used + n >= size)
        return;
    if (n == 1)
        out[(*used)++] = (char)c;
    else if (n == 2)
    {
        out[(*used)++] = (char)(192 | (c >> 6));
        out[(*used)++] = (char)(128 | (c & 63));
    }
    else
    {
        out[(*used)++] = (char)(224 | (c >> 12));
        out[(*used)++] = (char)(128 | ((c >> 6) & 63));
        out[(*used)++] = (char)(128 | (c & 63));
    }
}
static void text(char *out, const uint8_t *p, size_t n, unsigned encoding)
{
    size_t used = 0;
    if (encoding == 1 || encoding == 2)
    {
        bool little = encoding == 1;
        if (n >= 2 && ((p[0] == 255 && p[1] == 254) || (p[0] == 254 && p[1] == 255)))
        {
            little = p[0] == 255;
            p += 2;
            n -= 2;
        }
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            uint32_t c = little ? p[i] | (uint32_t)p[i + 1] << 8 : (uint32_t)p[i] << 8 | p[i + 1];
            if (!c)
                break;
            if (c >= 0xd800 && c <= 0xdfff)
                c = '?';
            utf8_put(out, AUDIO_TAG_TEXT, &used, c);
        }
    }
    else
        for (size_t i = 0; i < n && p[i]; ++i)
        {
            if (encoding == 0)
                utf8_put(out, AUDIO_TAG_TEXT, &used, p[i]);
            else if (used + 1 < AUDIO_TAG_TEXT)
                out[used++] = p[i] < 32 ? ' ' : (char)p[i];
        }
    while (used && out[used - 1] == ' ')
        --used;
    out[used] = 0;
}
void audio_metadata_display_text(const char *in,char *out,size_t bytes)
{ display_text_ascii(in,out,bytes,false); }
bool audio_metadata_art(AudioMetadata *m, const void *p, size_t n, unsigned type)
{
    if(!NDMEDIA_ARTWORK)return false;
    if (!n || n > AUDIO_ART_LIMIT || (m->art && (m->art_type == 3 || type != 3)))
        return false;
    const uint8_t *b = p;
    if (!((n >= 3 && b[0] == 255 && b[1] == 216 && b[2] == 255) ||
          (n >= 8 && !memcmp(b, "\211PNG\r\n\032\n", 8))))
        return false;
    uint8_t *copy = malloc(n);
    if (!copy)
        return false;
    memcpy(copy, p, n);
    free(m->art);
    m->art = copy;
    m->art_bytes = n;
    m->art_type = type;
    return true;
}
static void picture(AudioMetadata *m, const uint8_t *p, size_t n)
{
    if (n < 32)
        return;
    unsigned type = be32(p);
    uint32_t mime = be32(p + 4);
    size_t at = 8;
    if (mime > n - at || n - at - mime < 4)
        return;
    at += mime;
    uint32_t desc = be32(p + at);
    at += 4;
    if (desc > n - at || n - at - desc < 20)
        return;
    at += desc + 16;
    uint32_t bytes = be32(p + at);
    at += 4;
    if (bytes <= n - at)
        audio_metadata_art(m, p + at, bytes, type);
}
static void base64_picture(AudioMetadata *m, const uint8_t *p, size_t n)
{
    if (n > TAG_LIMIT)
        return;
    uint8_t *out = malloc(n / 4 * 3 + 3);
    if (!out)
        return;
    unsigned value = 0, bits = 0;
    size_t used = 0;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned c = p[i], v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+')
            v = 62;
        else if (c == '/')
            v = 63;
        else if (c == '=')
            break;
        else
        {
            free(out);
            return;
        }
        value = (value << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out[used++] = (uint8_t)(value >> bits);
        }
    }
    picture(m, out, used);
    free(out);
}
static bool key(const uint8_t *p, size_t n, const char *name)
{
    size_t len = strlen(name);
    if (n != len)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned c = p[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != (unsigned char)name[i])
            return false;
    }
    return true;
}
static void comments(AudioMetadata *m, const uint8_t *p, size_t n)
{
    if (n < 8)
        return;
    uint32_t vendor = le32(p);
    size_t at = 4;
    if (vendor > n - at || n - at - vendor < 4)
        return;
    at += vendor;
    uint32_t count = le32(p + at);
    at += 4;
    for (uint32_t i = 0; i < count && i < 4096 && n - at >= 4; ++i)
    {
        uint32_t bytes = le32(p + at);
        at += 4;
        if (bytes > n - at)
            break;
        const uint8_t *eq = memchr(p + at, '=', bytes);
        if (eq)
        {
            size_t k = (size_t)(eq - (p + at)), v = bytes - k - 1;
            char *dest = NULL;
            if (key(p + at, k, "TITLE"))
                dest = m->title;
            else if (key(p + at, k, "ARTIST"))
                dest = m->artist;
            else if (key(p + at, k, "ALBUM"))
                dest = m->album;
            else if (key(p + at, k, "METADATA_BLOCK_PICTURE"))
                base64_picture(m, eq + 1, v);
            if (dest)
                text(dest, eq + 1, v, 3);
        }
        at += bytes;
    }
}
static void flac(Reader *r, AudioMetadata *m)
{
    uint64_t at = 4;
    for (unsigned i = 0; i < 128; ++i)
    {
        uint8_t h[4];
        if (!read_at(r, at, h, 4))
            break;
        at += 4;
        uint32_t n = (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
        if (at > r->in->bytes || n > r->in->bytes - at)
            break;
        if ((h[0] & 127) == 4 || (h[0] & 127) == 6)
        {
            uint8_t *p = blob(r, at, n);
            if (p)
            {
                if ((h[0] & 127) == 4)
                    comments(m, p, n);
                else
                    picture(m, p, n);
                free(p);
            }
        }
        at += n;
        if (h[0] & 128)
            break;
    }
}
static void id3(AudioMetadata *m, uint8_t *p, size_t n, unsigned ver, bool unsync)
{
    if (ver < 3 || ver > 4)
        return;
    if (unsync)
    {
        size_t w = 0;
        for (size_t i = 0; i < n; ++i)
        {
            p[w++] = p[i];
            if (p[i] == 255 && i + 1 < n && !p[i + 1])
                ++i;
        }
        n = w;
    }
    size_t at = 0;
    for (unsigned i = 0; i < 4096 && n - at >= 10; ++i)
    {
        uint8_t *h = p + at;
        uint32_t bytes = ver == 4 ? sync32(h + 4) : be32(h + 4);
        at += 10;
        if (!h[0] || bytes > n - at)
            break;
        uint8_t *d = p + at;
        char *dest = NULL;
        /* Compression/encryption/unsynchronised individual frames are optional. */
        if (!(h[9] & (ver == 4 ? 0x4f : 0xe0)) && bytes)
        {
            if (!memcmp(h, "TIT2", 4))
                dest = m->title;
            else if (!memcmp(h, "TPE1", 4))
                dest = m->artist;
            else if (!memcmp(h, "TALB", 4))
                dest = m->album;
            if (dest)
                text(dest, d + 1, bytes - 1, d[0]);
            if (!memcmp(h, "APIC", 4) && bytes > 4)
            {
                size_t a = 1;
                while (a < bytes && d[a])
                    ++a;
                ++a;
                if (a < bytes)
                {
                    unsigned type = d[a++];
                    if (d[0] == 1 || d[0] == 2)
                    {
                        while (a + 1 < bytes && (d[a] || d[a + 1]))
                            a += 2;
                        a += 2;
                    }
                    else
                    {
                        while (a < bytes && d[a])
                            ++a;
                        ++a;
                    }
                    if (a < bytes)
                        audio_metadata_art(m, d + a, bytes - a, type);
                }
            }
        }
        at += bytes;
    }
}
static void mp4(Reader *r, AudioMetadata *m, uint64_t at, uint64_t end, unsigned depth,
                uint32_t field)
{
    if (depth > 8)
        return;
    while (end >= at && end - at >= 8 && r->nodes++ < 1024)
    {
        uint8_t h[16];
        if (!read_at(r, at, h, 8))
            return;
        uint64_t n = be32(h);
        size_t header = 8;
        if (n == 1)
        {
            if (!read_at(r, at + 8, h + 8, 8))
                return;
            n = (uint64_t)be32(h + 8) << 32 | be32(h + 12);
            header = 16;
        }
        if (!n)
            n = end - at;
        if (n < header || n > end - at)
            return;
        uint64_t start = at + header;
        uint32_t type = be32(h + 4);
        if (type == 0x64617461 && n >= header + 8 && n - header - 8 <= TAG_LIMIT)
        {
            size_t bytes = (size_t)(n - header - 8);
            uint8_t *p = blob(r, start + 8, bytes);
            if (p)
            {
                if (field == 0x636f7672)
                    audio_metadata_art(m, p, bytes, 3);
                else
                {
                    char *dest = field == 0xa96e616d   ? m->title
                                 : field == 0xa9415254 ? m->artist
                                 : field == 0xa9616c62 ? m->album
                                                       : NULL;
                    if (dest)
                        text(dest, p, bytes, 3);
                }
                free(p);
            }
        }
        else if (type == 0x6d6f6f76 || type == 0x75647461 || type == 0x6d657461 ||
                 type == 0x696c7374 || type == 0x636f7672 || type == 0xa96e616d ||
                 type == 0xa9415254 || type == 0xa9616c62)
        {
            if (type == 0x6d657461)
                start += 4;
            if (start <= at + n)
                mp4(r, m, start, at + n, depth + 1, type);
        }
        at += n;
    }
}
static void ogg(Reader *r, AudioMetadata *m)
{
    uint8_t *packet = malloc(TAG_LIMIT), *payload = malloc(65536);
    if (!packet || !payload)
    {
        free(packet);
        free(payload);
        return;
    }
    uint64_t at = 0;
    size_t used = 0;
    unsigned packets = 0;
    uint32_t serial = 0;
    bool have_serial = false;
    for (unsigned page = 0; page < 128 && packets < 3; ++page)
    {
        uint8_t h[27], lace[255];
        if (!read_at(r, at, h, 27) || memcmp(h, "OggS", 4) || h[4])
            break;
        if (!have_serial)
        {
            serial = le32(h + 14);
            have_serial = true;
        }
        unsigned seg = h[26];
        at += 27;
        if (!read_at(r, at, lace, seg))
            break;
        at += seg;
        unsigned total = 0;
        for (unsigned i = 0; i < seg; ++i)
            total += lace[i];
        if (!read_at(r, at, payload, total))
            break;
        size_t payload_at = 0;
        bool ours = le32(h + 14) == serial;
        for (unsigned i = 0; i < seg; ++i)
        {
            unsigned bytes = lace[i];
            if (ours)
            {
                if (used + bytes > TAG_LIMIT)
                    goto done;
                memcpy(packet + used, payload + payload_at, bytes);
                used += bytes;
                if (bytes < 255)
                {
                    ++packets;
                    if (used >= 8 && !memcmp(packet, "OpusTags", 8))
                        comments(m, packet + 8, used - 8);
                    else if (used >= 7 && !memcmp(packet, "\3vorbis", 7))
                        comments(m, packet + 7, used - 7);
                    used = 0;
                }
            }
            at += bytes;
            payload_at += bytes;
        }
    }
done:
    free(packet);
    free(payload);
}
void audio_metadata_read(AudioInput *in, AudioMetadata *m)
{
    memset(m, 0, sizeof(*m));
    if (!in || !in->seek || !in->read)
        return;
    m->file_bytes = in->bytes;
    Reader r = {in, TAG_READ_BUDGET, 0};
    uint8_t h[12];
    if (!read_at(&r, 0, h, sizeof(h)))
        return;
    if (!memcmp(h, "fLaC", 4))
        flac(&r, m);
    else if (!memcmp(h, "ID3", 3) && !(h[5] & 0x40))
    {
        size_t n = sync32(h + 6);
        uint8_t *p = blob(&r, 10, n);
        if (p)
        {
            id3(m, p, n, h[3], (h[5] & 0x80) != 0);
            free(p);
        }
    }
    else if (!memcmp(h, "OggS", 4))
        ogg(&r, m);
    else if (!memcmp(h + 4, "ftyp", 4))
        mp4(&r, m, 0, in->bytes, 0, 0);
}
void audio_metadata_close(AudioMetadata *m)
{
    if (m)
    {
        free(m->art);
        memset(m, 0, sizeof(*m));
    }
}
