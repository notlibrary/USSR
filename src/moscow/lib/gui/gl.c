/* gl.c - OpenGL 1.1 in software, the part Petersburg draws with:
 * glBegin/glEnd quads, lines, line loops and strips, colours, alpha
 * textures modulated and blended (TrueType glyphs), glDrawPixels with a
 * zoom, scissor and glClear, through an orthographic projection. It
 * draws into the pixels of the current context, one GLFW window (32-bit
 * 0x00RRGGBB, rows top down: glfw.c), with GL's rules: window
 * coordinates have y up, a pixel is drawn when its centre is inside. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLFW/glfw3.h>

typedef struct gl_texture
{
    GLuint id;
    void *owner;            /* the context it was made in */
    int width;
    int height;
    int channels;           /* 1 alpha, 3 RGB, 4 RGBA */
    unsigned char *texels;
    struct gl_texture *next;
} gl_texture_t;

typedef struct
{
    double x, y;            /* window coordinates, y up */
    float s, t;
} gl_vertex_t;

static struct
{
    void *owner;
    uint32_t *pixels;
    int width;
    int height;
    int vx, vy, vw, vh;
    double proj[16];
    double model[16];
    double *matrix;
    float r, g, b;
    float s, t;
    float clear_r, clear_g, clear_b;
    int scissor_on;
    int sx, sy, sw, sh;
    int blend_on;
    int texture_on;
    GLuint bound;
    int unpack;
    double raster_x, raster_y;
    float zoom_x, zoom_y;
    GLenum mode;
    gl_vertex_t v[4];
    int n;
    int first_set;
    gl_vertex_t first;
    gl_texture_t *textures;
    GLuint next_texture;
} gl;

static void gl_identity(double *m)
{
    int i;
    for (i = 0; i < 16; ++i)
        m[i] = (i % 5) == 0 ? 1.0 : 0.0;
}

/* the drawing target: called by glfwMakeContextCurrent */
static void gl_bind(void *owner, uint32_t *pixels, int width, int height)
{
    if (gl.matrix == NULL)
    {
        gl_identity(gl.proj);
        gl_identity(gl.model);
        gl.matrix = gl.model;
        gl.unpack = 4;
        gl.zoom_x = 1.0f;
        gl.zoom_y = 1.0f;
        gl.next_texture = 1;
        gl.r = gl.g = gl.b = 1.0f;
    }
    gl.owner = owner;
    gl.pixels = pixels;
    gl.width = width;
    gl.height = height;
}

/* the textures made in a context that is going away */
static void gl_forget(void *owner)
{
    gl_texture_t **link = &gl.textures;
    while (*link != NULL)
    {
        gl_texture_t *t = *link;
        if (t->owner == owner)
        {
            *link = t->next;
            free(t->texels);
            free(t);
        }
        else
            link = &t->next;
    }
    if (gl.owner == owner)
    {
        gl.owner = NULL;
        gl.pixels = NULL;
    }
}

static gl_texture_t *gl_texture(GLuint id)
{
    gl_texture_t *t;
    for (t = gl.textures; t != NULL; t = t->next)
        if (t->id == id)
            return t;
    return NULL;
}

static uint32_t gl_rgb(float r, float g, float b)
{
    int ri = (int)(r * 255.0f + 0.5f);
    int gi = (int)(g * 255.0f + 0.5f);
    int bi = (int)(b * 255.0f + 0.5f);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

static uint32_t gl_mix(uint32_t dst, uint32_t src, unsigned a)
{
    unsigned na = 255 - a;
    uint32_t rb = ((src & 0xff00ff) * a + (dst & 0xff00ff) * na + 0x800080) >> 8;
    uint32_t g = ((src & 0x00ff00) * a + (dst & 0x00ff00) * na + 0x008000) >> 8;
    return (rb & 0xff00ff) | (g & 0x00ff00);
}

/* the box drawing may touch, in rows top down: the buffer, the viewport's
 * neighbourhood is not clipped (GL clips to the window), the scissor */
static void gl_clip(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 0;
    *y0 = 0;
    *x1 = gl.width;
    *y1 = gl.height;
    if (gl.scissor_on)
    {
        int top = gl.height - (gl.sy + gl.sh);
        if (gl.sx > *x0) *x0 = gl.sx;
        if (gl.sx + gl.sw < *x1) *x1 = gl.sx + gl.sw;
        if (top > *y0) *y0 = top;
        if (gl.height - gl.sy < *y1) *y1 = gl.height - gl.sy;
    }
}

/* object coordinates to window coordinates (y up) */
static void gl_transform(double x, double y, double *wx, double *wy)
{
    const double *m = gl.model;
    const double *p = gl.proj;
    double ex = m[0] * x + m[4] * y + m[12];
    double ey = m[1] * x + m[5] * y + m[13];
    double ew = m[3] * x + m[7] * y + m[15];
    double cx = p[0] * ex + p[4] * ey + p[12] * ew;
    double cy = p[1] * ex + p[5] * ey + p[13] * ew;
    double cw = p[3] * ex + p[7] * ey + p[15] * ew;
    if (cw == 0.0)
        cw = 1.0;
    *wx = gl.vx + (cx / cw + 1.0) * 0.5 * gl.vw;
    *wy = gl.vy + (cy / cw + 1.0) * 0.5 * gl.vh;
}

static unsigned gl_texel_alpha(const gl_texture_t *t, float s, float u, uint32_t *rgb)
{
    int x = (int)floorf(s * t->width);
    int y = (int)floorf(u * t->height);
    const unsigned char *p;
    if (x < 0) x = 0; else if (x >= t->width) x = t->width - 1;
    if (y < 0) y = 0; else if (y >= t->height) y = t->height - 1;
    p = t->texels + ((size_t)y * t->width + x) * t->channels;
    if (t->channels == 1)
        return p[0];
    *rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return t->channels == 4 ? p[3] : 255;
}

/* a fragment at column x, row y (top down) */
static void gl_fragment(uint32_t *row, int x, uint32_t color, const gl_texture_t *tex,
                        float s, float t)
{
    if (tex != NULL)
    {
        uint32_t texel = color;
        unsigned a = gl_texel_alpha(tex, s, t, &texel);
        if (tex->channels != 1)
        {
            /* MODULATE: texel times the colour */
            uint32_t cr = (color >> 16) & 255, cg = (color >> 8) & 255, cb = color & 255;
            color = ((((texel >> 16) & 255) * cr / 255) << 16) |
                    ((((texel >> 8) & 255) * cg / 255) << 8) |
                    ((texel & 255) * cb / 255);
        }
        if (gl.blend_on)
        {
            if (a == 0)
                return;
            if (a < 255)
            {
                row[x] = gl_mix(row[x], color, a);
                return;
            }
        }
    }
    row[x] = color;
}

/* an axis-aligned rectangle of window coordinates, x0 < x1, y0 < y1 (y
 * up), with texture coordinates at its corners */
static void gl_rect(double x0, double y0, double x1, double y1,
                    float s0, float t0, float s1, float t1)
{
    int cx0, cy0, cx1, cy1;
    int c0, c1, r0, r1, r, c;
    uint32_t color = gl_rgb(gl.r, gl.g, gl.b);
    const gl_texture_t *tex = gl.texture_on ? gl_texture(gl.bound) : NULL;

    gl_clip(&cx0, &cy0, &cx1, &cy1);
    /* columns whose centres are inside */
    c0 = (int)ceil(x0 - 0.5);
    c1 = (int)ceil(x1 - 0.5);
    /* rows top down: row r has window y centre height - r - 0.5 */
    r0 = (int)ceil(gl.height - y1 - 0.5);
    r1 = (int)ceil(gl.height - y0 - 0.5);
    if (c0 < cx0) c0 = cx0;
    if (c1 > cx1) c1 = cx1;
    if (r0 < cy0) r0 = cy0;
    if (r1 > cy1) r1 = cy1;
    if (c0 >= c1 || r0 >= r1)
        return;
    if (tex == NULL)
    {
        for (r = r0; r < r1; ++r)
        {
            uint32_t *row = gl.pixels + (size_t)r * gl.width;
            for (c = c0; c < c1; ++c)
                row[c] = color;
        }
        return;
    }
    for (r = r0; r < r1; ++r)
    {
        uint32_t *row = gl.pixels + (size_t)r * gl.width;
        double wy = gl.height - r - 0.5;
        float ft = (float)(t0 + (t1 - t0) * (wy - y0) / (y1 - y0));
        for (c = c0; c < c1; ++c)
        {
            double wx = c + 0.5;
            float fs = (float)(s0 + (s1 - s0) * (wx - x0) / (x1 - x0));
            gl_fragment(row, c, color, tex, fs, ft);
        }
    }
}

static double gl_edge(const gl_vertex_t *a, const gl_vertex_t *b, double x, double y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

/* any triangle, by its bounding box and edge functions */
static void gl_triangle(const gl_vertex_t *a, const gl_vertex_t *b, const gl_vertex_t *c)
{
    double area = gl_edge(a, b, c->x, c->y);
    int cx0, cy0, cx1, cy1;
    double minx, maxx, miny, maxy;
    int c0, c1, r0, r1, r, col;
    uint32_t color = gl_rgb(gl.r, gl.g, gl.b);
    const gl_texture_t *tex = gl.texture_on ? gl_texture(gl.bound) : NULL;

    if (area == 0.0)
        return;
    gl_clip(&cx0, &cy0, &cx1, &cy1);
    minx = a->x < b->x ? a->x : b->x; if (c->x < minx) minx = c->x;
    maxx = a->x > b->x ? a->x : b->x; if (c->x > maxx) maxx = c->x;
    miny = a->y < b->y ? a->y : b->y; if (c->y < miny) miny = c->y;
    maxy = a->y > b->y ? a->y : b->y; if (c->y > maxy) maxy = c->y;
    c0 = (int)floor(minx); c1 = (int)ceil(maxx) + 1;
    r0 = (int)floor(gl.height - maxy); r1 = (int)ceil(gl.height - miny) + 1;
    if (c0 < cx0) c0 = cx0;
    if (c1 > cx1) c1 = cx1;
    if (r0 < cy0) r0 = cy0;
    if (r1 > cy1) r1 = cy1;
    for (r = r0; r < r1; ++r)
    {
        uint32_t *row = gl.pixels + (size_t)r * gl.width;
        double y = gl.height - r - 0.5;
        for (col = c0; col < c1; ++col)
        {
            double x = col + 0.5;
            double w0 = gl_edge(b, c, x, y) / area;
            double w1 = gl_edge(c, a, x, y) / area;
            double w2 = gl_edge(a, b, x, y) / area;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            gl_fragment(row, col, color, tex,
                        (float)(w0 * a->s + w1 * b->s + w2 * c->s),
                        (float)(w0 * a->t + w1 * b->t + w2 * c->t));
        }
    }
}

static void gl_quad(const gl_vertex_t *q)
{
    /* the usual case: a rectangle along the axes */
    if (q[0].y == q[1].y && q[1].x == q[2].x && q[2].y == q[3].y && q[3].x == q[0].x &&
        q[0].x != q[1].x && q[0].y != q[3].y)
    {
        const gl_vertex_t *lo_x = q[0].x < q[1].x ? &q[0] : &q[1];
        const gl_vertex_t *hi_x = q[0].x < q[1].x ? &q[1] : &q[0];
        const gl_vertex_t *lo_y = q[0].y < q[3].y ? &q[0] : &q[3];
        const gl_vertex_t *hi_y = q[0].y < q[3].y ? &q[3] : &q[0];
        gl_rect(lo_x->x, lo_y->y, hi_x->x, hi_y->y, lo_x->s, lo_y->t, hi_x->s, hi_y->t);
        return;
    }
    gl_triangle(&q[0], &q[1], &q[2]);
    gl_triangle(&q[0], &q[2], &q[3]);
}

static void gl_plot(int x, int r, uint32_t color, int cx0, int cy0, int cx1, int cy1)
{
    if (x >= cx0 && x < cx1 && r >= cy0 && r < cy1)
        gl.pixels[(size_t)r * gl.width + x] = color;
}

/* a line between window coordinates; the last pixel is left out, as GL's
 * diamond rule leaves it, so that strips do not draw joints twice */
static void gl_line(const gl_vertex_t *a, const gl_vertex_t *b)
{
    int cx0, cy0, cx1, cy1;
    int x0 = (int)floor(a->x), y0 = (int)floor(gl.height - a->y);
    int x1 = (int)floor(b->x), y1 = (int)floor(gl.height - b->y);
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    uint32_t color = gl_rgb(gl.r, gl.g, gl.b);

    gl_clip(&cx0, &cy0, &cx1, &cy1);
    if (dx > 4 * gl.width || -dy > 4 * gl.height)
        return;
    while (x0 != x1 || y0 != y1)
    {
        int e2 = 2 * err;
        gl_plot(x0, y0, color, cx0, cy0, cx1, cy1);
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* --- the API */

void glBegin(GLenum mode)
{
    gl.mode = mode;
    gl.n = 0;
    gl.first_set = 0;
}

void glEnd(void)
{
    if (gl.mode == GL_LINE_LOOP && gl.first_set && gl.n == 1)
        gl_line(&gl.v[0], &gl.first);
    gl.n = 0;
    gl.first_set = 0;
}

static void gl_vertex(double x, double y)
{
    gl_vertex_t v;
    if (gl.pixels == NULL)
        return;
    gl_transform(x, y, &v.x, &v.y);
    v.s = gl.s;
    v.t = gl.t;
    switch (gl.mode)
    {
    case GL_QUADS:
        gl.v[gl.n++] = v;
        if (gl.n == 4)
        {
            gl_quad(gl.v);
            gl.n = 0;
        }
        break;
    case GL_TRIANGLES:
        gl.v[gl.n++] = v;
        if (gl.n == 3)
        {
            gl_triangle(&gl.v[0], &gl.v[1], &gl.v[2]);
            gl.n = 0;
        }
        break;
    case GL_LINES:
        gl.v[gl.n++] = v;
        if (gl.n == 2)
        {
            gl_line(&gl.v[0], &gl.v[1]);
            gl.n = 0;
        }
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        if (!gl.first_set)
        {
            gl.first = v;
            gl.first_set = 1;
        }
        else
            gl_line(&gl.v[0], &v);
        gl.v[0] = v;
        gl.n = 1;
        break;
    default:
        break;
    }
}

void glVertex2f(GLfloat x, GLfloat y) { gl_vertex(x, y); }
void glVertex2d(GLdouble x, GLdouble y) { gl_vertex(x, y); }

void glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    gl.r = r;
    gl.g = g;
    gl.b = b;
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    gl.s = s;
    gl.t = t;
}

void glMatrixMode(GLenum mode)
{
    gl.matrix = mode == GL_PROJECTION ? gl.proj : gl.model;
}

void glLoadIdentity(void) { gl_identity(gl.matrix); }

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    double o[16];
    double m[16];
    int i, j, k;
    gl_identity(o);
    o[0] = 2.0 / (r - l);
    o[5] = 2.0 / (t - b);
    o[10] = -2.0 / (f - n);
    o[12] = -(r + l) / (r - l);
    o[13] = -(t + b) / (t - b);
    o[14] = -(f + n) / (f - n);
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
        {
            double s = 0;
            for (k = 0; k < 4; ++k)
                s += gl.matrix[k * 4 + j] * o[i * 4 + k];
            m[i * 4 + j] = s;
        }
    memcpy(gl.matrix, m, sizeof(m));
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    gl.vx = x;
    gl.vy = y;
    gl.vw = w;
    gl.vh = h;
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    gl.sx = x;
    gl.sy = y;
    gl.sw = w;
    gl.sh = h;
}

static void gl_set(GLenum cap, int on)
{
    if (cap == GL_SCISSOR_TEST)
        gl.scissor_on = on;
    else if (cap == GL_BLEND)
        gl.blend_on = on;
    else if (cap == GL_TEXTURE_2D)
        gl.texture_on = on;
}

void glEnable(GLenum cap) { gl_set(cap, 1); }
void glDisable(GLenum cap) { gl_set(cap, 0); }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; }
void glTexParameteri(GLenum target, GLenum name, GLint value) { (void)target; (void)name; (void)value; }
void glTexEnvi(GLenum target, GLenum name, GLint value) { (void)target; (void)name; (void)value; }

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    (void)a;
    gl.clear_r = r;
    gl.clear_g = g;
    gl.clear_b = b;
}

void glClear(GLbitfield mask)
{
    int x0, y0, x1, y1, r, c;
    uint32_t color = gl_rgb(gl.clear_r, gl.clear_g, gl.clear_b);
    if ((mask & GL_COLOR_BUFFER_BIT) == 0 || gl.pixels == NULL)
        return;
    gl_clip(&x0, &y0, &x1, &y1);
    for (r = y0; r < y1; ++r)
    {
        uint32_t *row = gl.pixels + (size_t)r * gl.width;
        for (c = x0; c < x1; ++c)
            row[c] = color;
    }
}

void glGenTextures(GLsizei n, GLuint *ids)
{
    GLsizei i;
    for (i = 0; i < n; ++i)
    {
        gl_texture_t *t = calloc(1, sizeof(*t));
        if (t == NULL)
        {
            ids[i] = 0;
            continue;
        }
        t->id = gl.next_texture++;
        t->owner = gl.owner;
        t->next = gl.textures;
        gl.textures = t;
        ids[i] = t->id;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *ids)
{
    GLsizei i;
    for (i = 0; i < n; ++i)
    {
        gl_texture_t **link = &gl.textures;
        while (*link != NULL && (*link)->id != ids[i])
            link = &(*link)->next;
        if (*link != NULL)
        {
            gl_texture_t *t = *link;
            *link = t->next;
            free(t->texels);
            free(t);
        }
    }
}

void glBindTexture(GLenum target, GLuint id)
{
    (void)target;
    gl.bound = id;
}

static int gl_channels(GLenum format)
{
    return format == GL_ALPHA ? 1 : format == GL_RGB ? 3 : format == GL_RGBA ? 4 : 0;
}

void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    gl_texture_t *t = gl_texture(gl.bound);
    int ch = gl_channels(format);
    size_t row;
    int y;
    (void)target; (void)level; (void)internal; (void)border;
    if (t == NULL || ch == 0 || type != GL_UNSIGNED_BYTE || w <= 0 || h <= 0)
        return;
    free(t->texels);
    t->texels = malloc((size_t)w * h * ch);
    if (t->texels == NULL)
        return;
    t->width = w;
    t->height = h;
    t->channels = ch;
    row = ((size_t)w * ch + gl.unpack - 1) / gl.unpack * gl.unpack;
    for (y = 0; y < h; ++y)
        memcpy(t->texels + (size_t)y * w * ch,
               (const unsigned char *)pixels + y * row, (size_t)w * ch);
}

void glPixelStorei(GLenum name, GLint value)
{
    if (name == GL_UNPACK_ALIGNMENT && (value == 1 || value == 2 || value == 4 || value == 8))
        gl.unpack = value;
}

void glRasterPos2i(GLint x, GLint y)
{
    gl_transform(x, y, &gl.raster_x, &gl.raster_y);
}

void glPixelZoom(GLfloat x, GLfloat y)
{
    gl.zoom_x = x;
    gl.zoom_y = y;
}

/* source pixel (i, j), j counted from the bottom row of the data, covers
 * the window box from (rx + zx i, ry + zy j) to (rx + zx (i+1), ry + zy (j+1)) */
void glDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels)
{
    int ch = gl_channels(format);
    double xa, xb, ya, yb;
    int cx0, cy0, cx1, cy1, c0, c1, r0, r1, r, c;
    size_t row_bytes;

    if (gl.pixels == NULL || ch == 0 || type != GL_UNSIGNED_BYTE || w <= 0 || h <= 0 ||
        gl.zoom_x == 0.0f || gl.zoom_y == 0.0f)
        return;
    row_bytes = ((size_t)w * ch + gl.unpack - 1) / gl.unpack * gl.unpack;
    xa = gl.raster_x;
    xb = gl.raster_x + gl.zoom_x * w;
    ya = gl.raster_y;
    yb = gl.raster_y + gl.zoom_y * h;
    if (xa > xb) { double s = xa; xa = xb; xb = s; }
    if (ya > yb) { double s = ya; ya = yb; yb = s; }
    gl_clip(&cx0, &cy0, &cx1, &cy1);
    c0 = (int)ceil(xa - 0.5);
    c1 = (int)ceil(xb - 0.5);
    r0 = (int)ceil(gl.height - yb - 0.5);
    r1 = (int)ceil(gl.height - ya - 0.5);
    if (c0 < cx0) c0 = cx0;
    if (c1 > cx1) c1 = cx1;
    if (r0 < cy0) r0 = cy0;
    if (r1 > cy1) r1 = cy1;
    for (r = r0; r < r1; ++r)
    {
        uint32_t *out = gl.pixels + (size_t)r * gl.width;
        double wy = gl.height - r - 0.5;
        int j = (int)floor((wy - gl.raster_y) / gl.zoom_y);
        const unsigned char *src;
        if (j < 0) j = 0; else if (j >= h) j = h - 1;
        src = (const unsigned char *)pixels + (size_t)j * row_bytes;
        for (c = c0; c < c1; ++c)
        {
            int i = (int)floor((c + 0.5 - gl.raster_x) / gl.zoom_x);
            const unsigned char *p;
            if (i < 0) i = 0; else if (i >= w) i = w - 1;
            p = src + (size_t)i * ch;
            if (ch == 1)
                out[c] = ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
            else
                out[c] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
}
