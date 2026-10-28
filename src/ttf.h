/*
 * ttf.h - a small TrueType reader and rasterizer for Petersburg's font text.
 *
 * Reads the outline fonts installed on the system (.ttf, and the first face
 * of a .ttc collection) with TrueType ("glyf") outlines; CFF-based OpenType
 * fonts are refused. Glyphs are turned into 8-bit coverage bitmaps by
 * accumulating the signed area every outline edge covers in each pixel, so
 * edges are anti-aliased without supersampling. No hinting, no kerning.
 *
 * Everything is static: petersburg.c includes this file once.
 */
#ifndef USSR_TTF_H
#define USSR_TTF_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    unsigned char *data;
    size_t size;
    uint32_t cmap;          /* chosen cmap subtable */
    int cmap_format;        /* 4 or 12 */
    uint32_t loca;
    uint32_t glyf;
    uint32_t glyf_size;
    uint32_t hmtx;
    int long_loca;
    int glyph_count;
    int hmetric_count;
    int units_per_em;
    int ascent;
    int descent;            /* negative, as in hhea */
    int line_gap;
} ttf_font_t;

/* A flattened outline edge in bitmap pixels, y growing downward. */
typedef struct
{
    float x0, y0, x1, y1;
} ttf_edge_t;

typedef struct
{
    ttf_edge_t *edges;
    size_t count;
    size_t capacity;
    float scale;
} ttf_outline_t;

static unsigned ttf_u16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static int ttf_s16(const unsigned char *p)
{
    return (int)(int16_t)ttf_u16(p);
}

static uint32_t ttf_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Offset of the first font of a .ttc, 0 for a plain font, or -1. */
static long ttf_face_offset(const unsigned char *head, size_t size)
{
    if (size < 12)
        return -1;
    if (memcmp(head, "ttcf", 4) == 0)
        return (long)ttf_u32(head + 12);
    return 0;
}

/* Finds a table in the directory at dir (inside data of size bytes). */
static int ttf_find_table(const unsigned char *data, size_t size,
                          uint32_t dir, const char *tag,
                          uint32_t *offset, uint32_t *length)
{
    unsigned count;
    unsigned i;

    if ((size_t)dir + 12 > size)
        return 0;
    count = ttf_u16(data + dir + 4);
    if ((size_t)dir + 12 + (size_t)count * 16 > size)
        return 0;
    for (i = 0; i < count; ++i)
    {
        const unsigned char *rec = data + dir + 12 + i * 16;
        if (memcmp(rec, tag, 4) == 0)
        {
            *offset = ttf_u32(rec + 8);
            *length = ttf_u32(rec + 12);
            return (size_t)*offset + *length <= size;
        }
    }
    return 0;
}

/* Takes ownership of data on success. */
static int ttf_load(ttf_font_t *font, unsigned char *data, size_t size)
{
    long face = ttf_face_offset(data, size);
    uint32_t dir;
    uint32_t off, len;
    uint32_t head, hhea, maxp, cmap;
    unsigned tables, i;
    uint32_t best = 0;
    int best_format = 0;

    memset(font, 0, sizeof(*font));
    if (face < 0 || (size_t)face + 12 > size)
        return 0;
    dir = (uint32_t)face;
    if (!ttf_find_table(data, size, dir, "head", &head, &len) || len < 54 ||
        !ttf_find_table(data, size, dir, "hhea", &hhea, &len) || len < 36 ||
        !ttf_find_table(data, size, dir, "maxp", &maxp, &len) || len < 6 ||
        !ttf_find_table(data, size, dir, "cmap", &cmap, &len) || len < 4 ||
        !ttf_find_table(data, size, dir, "loca", &font->loca, &len) ||
        !ttf_find_table(data, size, dir, "glyf", &font->glyf,
                        &font->glyf_size) ||
        !ttf_find_table(data, size, dir, "hmtx", &font->hmtx, &len))
        return 0;
    font->units_per_em = (int)ttf_u16(data + head + 18);
    font->long_loca = ttf_s16(data + head + 50) != 0;
    font->glyph_count = (int)ttf_u16(data + maxp + 4);
    font->ascent = ttf_s16(data + hhea + 4);
    font->descent = ttf_s16(data + hhea + 6);
    font->line_gap = ttf_s16(data + hhea + 8);
    font->hmetric_count = (int)ttf_u16(data + hhea + 34);
    if (font->units_per_em <= 0 || font->hmetric_count <= 0 ||
        (size_t)font->loca + (size_t)(font->glyph_count + 1) *
            (font->long_loca ? 4 : 2) > size ||
        (size_t)font->hmtx + (size_t)font->hmetric_count * 4 > size)
        return 0;

    /* Unicode subtables: full repertoire (format 12) over BMP (format 4) */
    tables = ttf_u16(data + cmap + 2);
    for (i = 0; i < tables; ++i)
    {
        const unsigned char *rec = data + cmap + 4 + i * 8;
        unsigned platform, encoding, format;
        uint32_t sub;

        if ((size_t)(rec - data) + 8 > size)
            break;
        platform = ttf_u16(rec);
        encoding = ttf_u16(rec + 2);
        sub = cmap + ttf_u32(rec + 4);
        if ((size_t)sub + 4 > size)
            continue;
        format = ttf_u16(data + sub);
        if (!((platform == 3 && (encoding == 1 || encoding == 10)) ||
              platform == 0))
            continue;
        if (format == 12 && best_format != 12)
        {
            best = sub;
            best_format = 12;
        }
        else if (format == 4 && best_format == 0)
        {
            best = sub;
            best_format = 4;
        }
    }
    if (best_format == 0)
        return 0;
    off = 0;
    (void)off;
    font->cmap = best;
    font->cmap_format = best_format;
    font->data = data;
    font->size = size;
    return 1;
}

static int ttf_glyph_index(const ttf_font_t *font, uint32_t codepoint)
{
    const unsigned char *data = font->data;
    uint32_t sub = font->cmap;

    if (font->cmap_format == 12)
    {
        uint32_t groups = ttf_u32(data + sub + 12);
        uint32_t lo = 0, hi = groups;
        if ((size_t)sub + 16 + (size_t)groups * 12 > font->size)
            return 0;
        while (lo < hi)
        {
            uint32_t mid = (lo + hi) / 2;
            const unsigned char *g = data + sub + 16 + mid * 12;
            uint32_t start = ttf_u32(g), end = ttf_u32(g + 4);
            if (codepoint < start)
                hi = mid;
            else if (codepoint > end)
                lo = mid + 1;
            else
                return (int)(ttf_u32(g + 8) + (codepoint - start));
        }
        return 0;
    }
    if (codepoint > 0xFFFF)
        return 0;
    {
        unsigned segs = ttf_u16(data + sub + 6) / 2;
        uint32_t ends = sub + 14;
        uint32_t starts = ends + segs * 2 + 2;
        uint32_t deltas = starts + segs * 2;
        uint32_t ranges = deltas + segs * 2;
        unsigned i;

        if ((size_t)ranges + segs * 2 > font->size)
            return 0;
        for (i = 0; i < segs; ++i)
        {
            unsigned end = ttf_u16(data + ends + i * 2);
            unsigned start, range;
            if (codepoint > end)
                continue;
            start = ttf_u16(data + starts + i * 2);
            if (codepoint < start)
                return 0;
            range = ttf_u16(data + ranges + i * 2);
            if (range == 0)
                return (int)((codepoint + ttf_u16(data + deltas + i * 2)) &
                             0xFFFF);
            {
                size_t at = (size_t)ranges + i * 2 + range +
                            (codepoint - start) * 2;
                unsigned glyph;
                if (at + 2 > font->size)
                    return 0;
                glyph = ttf_u16(data + at);
                if (glyph == 0)
                    return 0;
                return (int)((glyph + ttf_u16(data + deltas + i * 2)) &
                             0xFFFF);
            }
        }
    }
    return 0;
}

/* Advance width in font units. */
static int ttf_advance(const ttf_font_t *font, int glyph)
{
    if (glyph >= font->hmetric_count)
        glyph = font->hmetric_count - 1;
    return (int)ttf_u16(font->data + font->hmtx + glyph * 4);
}

/* Byte range of a glyph's outline in glyf, 0 when it has none. */
static int ttf_glyph_range(const ttf_font_t *font, int glyph,
                           uint32_t *start, uint32_t *end)
{
    const unsigned char *loca = font->data + font->loca;

    if (glyph < 0 || glyph >= font->glyph_count)
        return 0;
    if (font->long_loca)
    {
        *start = ttf_u32(loca + glyph * 4);
        *end = ttf_u32(loca + glyph * 4 + 4);
    }
    else
    {
        *start = ttf_u16(loca + glyph * 2) * 2u;
        *end = ttf_u16(loca + glyph * 2 + 2) * 2u;
    }
    if (*end <= *start || *end > font->glyf_size)
        return 0;
    *start += font->glyf;
    *end += font->glyf;
    return 1;
}

static void ttf_add_edge(ttf_outline_t *out, float x0, float y0,
                         float x1, float y1)
{
    if (y0 == y1)
        return;
    if (out->count == out->capacity)
    {
        size_t capacity = out->capacity ? out->capacity * 2 : 64;
        ttf_edge_t *grown = realloc(out->edges, capacity * sizeof(*grown));
        if (grown == NULL)
            return;
        out->edges = grown;
        out->capacity = capacity;
    }
    out->edges[out->count].x0 = x0;
    out->edges[out->count].y0 = y0;
    out->edges[out->count].x1 = x1;
    out->edges[out->count].y1 = y1;
    ++out->count;
}

/* A quadratic Bezier as straight pieces short enough to look round. */
static void ttf_add_curve(ttf_outline_t *out, float x0, float y0,
                          float cx, float cy, float x1, float y1)
{
    float dx = x0 - 2 * cx + x1, dy = y0 - 2 * cy + y1;
    int n = 1 + (int)sqrtf(sqrtf(dx * dx + dy * dy) * 4.0f);
    float px = x0, py = y0;
    int i;

    if (n > 24)
        n = 24;
    for (i = 1; i <= n; ++i)
    {
        float t = (float)i / (float)n, u = 1 - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
        float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        ttf_add_edge(out, px, py, x, y);
        px = x;
        py = y;
    }
}

/*
 * Appends the outline of glyph to out, in pixels with y down: a font-unit
 * point (x, y) goes through the affine transform m (from composite glyphs)
 * and lands at (x * scale, -y * scale).
 */
static void ttf_glyph_edges(const ttf_font_t *font, int glyph,
                            const float m[6], ttf_outline_t *out, int depth)
{
    const unsigned char *data = font->data;
    uint32_t start, end;
    int contours;
    float s = out->scale;

    if (depth > 8 || !ttf_glyph_range(font, glyph, &start, &end) ||
        end - start < 10)
        return;
    contours = ttf_s16(data + start);
    if (contours < 0)
    {
        /* composite: parts placed by offsets and an optional 2x2 matrix */
        uint32_t p = start + 10;
        unsigned flags;
        do
        {
            float a = 1, b = 0, c = 0, d = 1, e, f, k[6];
            int part;
            if (p + 4 > end)
                return;
            flags = ttf_u16(data + p);
            part = (int)ttf_u16(data + p + 2);
            p += 4;
            if (flags & 1)
            {
                e = (float)ttf_s16(data + p);
                f = (float)ttf_s16(data + p + 2);
                p += 4;
            }
            else
            {
                e = (float)(signed char)data[p];
                f = (float)(signed char)data[p + 1];
                p += 2;
            }
            if (!(flags & 2))
                e = f = 0; /* point matching: not supported, place at 0 */
            if (flags & 8)
            {
                a = d = ttf_s16(data + p) / 16384.0f;
                p += 2;
            }
            else if (flags & 0x40)
            {
                a = ttf_s16(data + p) / 16384.0f;
                d = ttf_s16(data + p + 2) / 16384.0f;
                p += 4;
            }
            else if (flags & 0x80)
            {
                a = ttf_s16(data + p) / 16384.0f;
                b = ttf_s16(data + p + 2) / 16384.0f;
                c = ttf_s16(data + p + 4) / 16384.0f;
                d = ttf_s16(data + p + 6) / 16384.0f;
                p += 8;
            }
            k[0] = m[0] * a + m[2] * b;
            k[1] = m[1] * a + m[3] * b;
            k[2] = m[0] * c + m[2] * d;
            k[3] = m[1] * c + m[3] * d;
            k[4] = m[0] * e + m[2] * f + m[4];
            k[5] = m[1] * e + m[3] * f + m[5];
            ttf_glyph_edges(font, part, k, out, depth + 1);
        } while (flags & 0x20);
        return;
    }
    {
        uint32_t ends = start + 10;
        uint32_t p = ends + (uint32_t)contours * 2;
        int points, i, ci, first;
        unsigned char *flags;
        float *xs, *ys;
        int x = 0, y = 0;

        if (contours == 0 || p + 2 > end)
            return;
        points = (int)ttf_u16(data + ends + (contours - 1) * 2) + 1;
        p += 2 + ttf_u16(data + p);
        flags = malloc((size_t)points);
        xs = malloc((size_t)points * sizeof(float));
        ys = malloc((size_t)points * sizeof(float));
        if (flags == NULL || xs == NULL || ys == NULL)
            goto done;
        for (i = 0; i < points;)
        {
            unsigned char fl;
            int repeat = 0;
            if (p >= end)
                goto done;
            fl = data[p++];
            if (fl & 8)
            {
                if (p >= end)
                    goto done;
                repeat = data[p++];
            }
            while (repeat-- >= 0 && i < points)
                flags[i++] = fl;
        }
        for (i = 0; i < points; ++i)
        {
            if (flags[i] & 2)
            {
                if (p >= end)
                    goto done;
                x += (flags[i] & 16) ? data[p] : -(int)data[p];
                ++p;
            }
            else if (!(flags[i] & 16))
            {
                if (p + 2 > end)
                    goto done;
                x += ttf_s16(data + p);
                p += 2;
            }
            xs[i] = (float)x;
        }
        for (i = 0; i < points; ++i)
        {
            if (flags[i] & 4)
            {
                if (p >= end)
                    goto done;
                y += (flags[i] & 32) ? data[p] : -(int)data[p];
                ++p;
            }
            else if (!(flags[i] & 32))
            {
                if (p + 2 > end)
                    goto done;
                y += ttf_s16(data + p);
                p += 2;
            }
            ys[i] = (float)y;
        }
        /* to pixels */
        for (i = 0; i < points; ++i)
        {
            float fx = m[0] * xs[i] + m[2] * ys[i] + m[4];
            float fy = m[1] * xs[i] + m[3] * ys[i] + m[5];
            xs[i] = fx * s;
            ys[i] = -fy * s;
        }
        first = 0;
        for (ci = 0; ci < contours; ++ci)
        {
            int last = (int)ttf_u16(data + ends + ci * 2);
            int n = last - first + 1;
            float sx, sy, px, py, cx = 0, cy = 0;
            int have_control = 0, j;

            if (last >= points || n < 2)
            {
                first = last + 1;
                continue;
            }
            /* start on an on-curve point (or between two off ones) */
            if (flags[first] & 1)
            {
                sx = xs[first];
                sy = ys[first];
            }
            else if (flags[last] & 1)
            {
                sx = xs[last];
                sy = ys[last];
            }
            else
            {
                sx = (xs[first] + xs[last]) / 2;
                sy = (ys[first] + ys[last]) / 2;
            }
            px = sx;
            py = sy;
            for (j = 0; j <= n; ++j)
            {
                int k = first + (j % n);
                float qx = xs[k], qy = ys[k];
                if (j == 0 && (flags[first] & 1))
                    continue;
                if (flags[k] & 1)
                {
                    if (have_control)
                        ttf_add_curve(out, px, py, cx, cy, qx, qy);
                    else
                        ttf_add_edge(out, px, py, qx, qy);
                    px = qx;
                    py = qy;
                    have_control = 0;
                }
                else
                {
                    if (have_control)
                    {
                        float mx = (cx + qx) / 2, my = (cy + qy) / 2;
                        ttf_add_curve(out, px, py, cx, cy, mx, my);
                        px = mx;
                        py = my;
                    }
                    cx = qx;
                    cy = qy;
                    have_control = 1;
                }
            }
            if (have_control)
                ttf_add_curve(out, px, py, cx, cy, sx, sy);
            else
                ttf_add_edge(out, px, py, sx, sy);
            first = last + 1;
        }
done:
        free(flags);
        free(xs);
        free(ys);
    }
}

/*
 * Coverage of the edges (already shifted into 0..width, 0..height) into
 * pixels, 0..255. acc holds width * height + 2 floats.
 */
static void ttf_raster(const ttf_outline_t *outline, int width, int height,
                       float *acc, unsigned char *pixels)
{
    size_t e;
    size_t total = (size_t)width * height;
    size_t i;
    float sum = 0;

    memset(acc, 0, (total + 2) * sizeof(float));
    for (e = 0; e < outline->count; ++e)
    {
        const ttf_edge_t *edge = &outline->edges[e];
        float x0 = edge->x0, y0 = edge->y0, x1 = edge->x1, y1 = edge->y1;
        float dir = 1, dxdy, x;
        int y, ystart, yend;

        if (y0 > y1)
        {
            float t;
            t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            dir = -1;
        }
        dxdy = (x1 - x0) / (y1 - y0);
        x = x0;
        if (y0 < 0)
        {
            x -= y0 * dxdy;
            y0 = 0;
        }
        if (y1 > (float)height)
            y1 = (float)height;
        ystart = (int)floorf(y0);
        yend = (int)ceilf(y1);
        for (y = ystart; y < yend; ++y)
        {
            size_t line = (size_t)y * width;
            float dy = ((float)(y + 1) < y1 ? (float)(y + 1) : y1) -
                       ((float)y > y0 ? (float)y : y0);
            float xnext = x + dxdy * dy;
            float d = dy * dir;
            float a = x < xnext ? x : xnext, b = x < xnext ? xnext : x;
            int ai, bi;

            if (a < 0)
                a = 0;
            if (b > (float)width - 0.001f)
                b = (float)width - 0.001f;
            if (b < a)
                b = a;
            ai = (int)floorf(a);
            bi = (int)ceilf(b);
            if (bi <= ai + 1)
            {
                float mid = 0.5f * (x + xnext) - (float)ai;
                if (mid < 0)
                    mid = 0;
                if (mid > 1)
                    mid = 1;
                acc[line + ai] += d - d * mid;
                acc[line + ai + 1] += d * mid;
            }
            else
            {
                float inv = 1.0f / (b - a);
                float af = a - (float)ai;
                float a0 = 0.5f * inv * (1 - af) * (1 - af);
                float bf = b - (float)bi + 1;
                float am = 0.5f * inv * bf * bf;
                int xi;
                acc[line + ai] += d * a0;
                if (bi == ai + 2)
                    acc[line + ai + 1] += d * (1 - a0 - am);
                else
                {
                    float a1 = inv * (1.5f - af);
                    float a2;
                    acc[line + ai + 1] += d * (a1 - a0);
                    for (xi = ai + 2; xi < bi - 1; ++xi)
                        acc[line + xi] += d * inv;
                    a2 = a1 + (float)(bi - ai - 3) * inv;
                    acc[line + bi - 1] += d * (1 - a2 - am);
                }
                acc[line + bi] += d * am;
            }
            x = xnext;
        }
    }
    for (i = 0; i < total; ++i)
    {
        float v;
        sum += acc[i];
        v = fabsf(sum);
        if (v > 1)
            v = 1;
        pixels[i] = (unsigned char)(v * 255.0f + 0.5f);
    }
}

/*
 * A glyph's coverage bitmap at px pixels per em; the bitmap's top-left
 * goes at (pen x + *left, baseline + *top). NULL for an empty glyph.
 */
static unsigned char *ttf_render_glyph(const ttf_font_t *font, int glyph,
                                       float px, int *width, int *height,
                                       int *left, int *top)
{
    static const float identity[6] = {1, 0, 0, 1, 0, 0};
    ttf_outline_t outline;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    unsigned char *pixels = NULL;
    float *acc;
    size_t i;
    int w, h, l, t;

    memset(&outline, 0, sizeof(outline));
    outline.scale = px / (float)font->units_per_em;
    ttf_glyph_edges(font, glyph, identity, &outline, 0);
    if (outline.count == 0)
    {
        free(outline.edges);
        return NULL;
    }
    for (i = 0; i < outline.count; ++i)
    {
        const ttf_edge_t *e = &outline.edges[i];
        if (e->x0 < minx) minx = e->x0;
        if (e->x1 < minx) minx = e->x1;
        if (e->x0 > maxx) maxx = e->x0;
        if (e->x1 > maxx) maxx = e->x1;
        if (e->y0 < miny) miny = e->y0;
        if (e->y1 < miny) miny = e->y1;
        if (e->y0 > maxy) maxy = e->y0;
        if (e->y1 > maxy) maxy = e->y1;
    }
    l = (int)floorf(minx);
    t = (int)floorf(miny);
    w = (int)ceilf(maxx) - l + 1;
    h = (int)ceilf(maxy) - t + 1;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
    {
        free(outline.edges);
        return NULL;
    }
    for (i = 0; i < outline.count; ++i)
    {
        outline.edges[i].x0 -= (float)l;
        outline.edges[i].x1 -= (float)l;
        outline.edges[i].y0 -= (float)t;
        outline.edges[i].y1 -= (float)t;
    }
    acc = malloc(((size_t)w * h + 2) * sizeof(float));
    pixels = malloc((size_t)w * h);
    if (acc != NULL && pixels != NULL)
        ttf_raster(&outline, w, h, acc, pixels);
    else
    {
        free(pixels);
        pixels = NULL;
    }
    free(acc);
    free(outline.edges);
    *width = w;
    *height = h;
    *left = l;
    *top = t;
    return pixels;
}

/* Copies a name-table string as UTF-8 (UTF-16BE or Mac Roman ASCII). */
static void ttf_name_copy(const unsigned char *s, unsigned length,
                          int utf16, char *out, size_t out_size)
{
    size_t n = 0;
    unsigned i;

    for (i = 0; i < length && n + 4 < out_size;)
    {
        unsigned c;
        if (utf16)
        {
            if (i + 1 >= length)
                break;
            c = ttf_u16(s + i);
            i += 2;
        }
        else
            c = s[i++];
        if (c < 32 || (c >= 0xD800 && c < 0xE000) || c == '|')
            continue;
        if (c < 0x80)
            out[n++] = (char)c;
        else if (c < 0x800)
        {
            out[n++] = (char)(0xC0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 63));
        }
        else
        {
            out[n++] = (char)(0xE0 | (c >> 12));
            out[n++] = (char)(0x80 | ((c >> 6) & 63));
            out[n++] = (char)(0x80 | (c & 63));
        }
    }
    out[n] = '\0';
}

/*
 * Family and style (name IDs 1 and 2, English when there is a choice) of
 * the font file at path, without reading all of it. 0 when it is not a
 * TrueType-outline font this reader can draw.
 */
static int ttf_file_info(const char *path, char *family, size_t family_size,
                         char *style, size_t style_size)
{
    FILE *file = fopen(path, "rb");
    unsigned char head[16];
    unsigned char *dir = NULL, *names = NULL;
    long face;
    unsigned count, i;
    uint32_t name_off = 0, name_len = 0;
    int has_glyf = 0, ok = 0;
    int best_family = -1, best_style = -1;

    family[0] = style[0] = '\0';
    if (file == NULL)
        return 0;
    if (fread(head, 1, 16, file) != 16 ||
        (face = ttf_face_offset(head, 16)) < 0 ||
        fseek(file, face, SEEK_SET) != 0 ||
        fread(head, 1, 12, file) != 12)
        goto done;
    count = ttf_u16(head + 4);
    if (count == 0 || count > 200 ||
        (dir = malloc((size_t)count * 16)) == NULL ||
        fread(dir, 16, count, file) != count)
        goto done;
    for (i = 0; i < count; ++i)
    {
        if (memcmp(dir + i * 16, "glyf", 4) == 0)
            has_glyf = 1;
        if (memcmp(dir + i * 16, "name", 4) == 0)
        {
            name_off = ttf_u32(dir + i * 16 + 8);
            name_len = ttf_u32(dir + i * 16 + 12);
        }
    }
    if (!has_glyf || name_len < 6 || name_len > (1u << 20) ||
        (names = malloc(name_len)) == NULL ||
        fseek(file, (long)name_off, SEEK_SET) != 0 ||
        fread(names, 1, name_len, file) != name_len)
        goto done;
    {
        unsigned records = ttf_u16(names + 2);
        unsigned strings = ttf_u16(names + 4);
        for (i = 0; i < records && 6 + (i + 1) * 12 <= name_len; ++i)
        {
            const unsigned char *r = names + 6 + i * 12;
            unsigned platform = ttf_u16(r), encoding = ttf_u16(r + 2);
            unsigned language = ttf_u16(r + 4), id = ttf_u16(r + 6);
            unsigned length = ttf_u16(r + 8), offset = ttf_u16(r + 10);
            int rank;
            char *dest;
            size_t dest_size;
            int *best;

            if (id != 1 && id != 2)
                continue;
            if (strings + offset + length > name_len)
                continue;
            if (platform == 3 && (encoding == 1 || encoding == 0))
                rank = language == 0x409 ? 3 : 2;
            else if (platform == 1 && encoding == 0)
                rank = language == 0 ? 1 : 0;
            else
                continue;
            best = id == 1 ? &best_family : &best_style;
            if (rank <= *best)
                continue;
            *best = rank;
            dest = id == 1 ? family : style;
            dest_size = id == 1 ? family_size : style_size;
            ttf_name_copy(names + strings + offset, length, platform == 3,
                          dest, dest_size);
        }
    }
    ok = family[0] != '\0';
    if (ok && style[0] == '\0')
        snprintf(style, style_size, "Regular");
done:
    free(dir);
    free(names);
    fclose(file);
    return ok;
}

/* Next code point of UTF-8 text; malformed bytes stand for themselves. */
static uint32_t ttf_utf8_next(const unsigned char **text)
{
    const unsigned char *s = *text;
    uint32_t c = *s++;
    int extra = 0;

    if (c >= 0xF0 && c < 0xF8)
    {
        c &= 7;
        extra = 3;
    }
    else if (c >= 0xE0)
    {
        c &= 15;
        extra = 2;
    }
    else if (c >= 0xC0)
    {
        c &= 31;
        extra = 1;
    }
    else
    {
        *text = s;
        return c;
    }
    while (extra-- > 0)
    {
        if ((*s & 0xC0) != 0x80)
        {
            *text = *text + 1;
            return (*text)[-1];
        }
        c = (c << 6) | (*s++ & 63);
    }
    *text = s;
    return c;
}

#endif
