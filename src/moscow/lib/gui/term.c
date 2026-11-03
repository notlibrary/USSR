/* term.c - the terminal of Mbltishi (src/mbltishi): a grid of
 * characters over a Petersburg canvas, written by the kernel's console
 * (print, what read_line echoes) and read by it (the keys typed into the
 * window), with the ANSI sequences Moscow's VGA console knows - cursor
 * moves, erasing, colours - so the shell and the Mbl editor run in it.
 * Text is UTF-8, drawn with a monospace TrueType font (Pobeda, the
 * project's own). */

#define TERM_MAX 8
#define TERM_INPUT 1024
#define TERM_GLYPHS 512

typedef struct
{
    uint32_t cp;
    unsigned char fg, bg;
} term_cell_t;

typedef struct
{
    uint32_t cp;
    unsigned char *alpha;
    int width, height, left, top;
} term_glyph_t;

typedef struct
{
    int used;
    long canvas;            /* the Petersburg canvas it covers */
    long window;
    long font;              /* a Petersburg font handle */
    int px;
    int cell_w, cell_h, ascent;
    int cols, rows;
    term_cell_t *cells;
    int cx, cy;
    int wrap_next;
    int saved_x, saved_y;
    unsigned char fg, bg;
    int bold, reverse;
    int state;              /* 0 text, 1 after ESC, 2 in CSI */
    int params[8];
    int nparams;
    int private_mode;
    int cursor_on;
    uint32_t utf8;
    int utf8_left;
    unsigned char input[TERM_INPUT];
    int in_head, in_tail;
    int closed;
    int dirty;
    term_glyph_t glyphs[TERM_GLYPHS];
} term_t;

static term_t terms[TERM_MAX];
static term_t *term_active;

/* the kernel's raw mode (terminal_raw): keys as ANSI sequences then */
extern long moscow_term_raw(void);

static const uint32_t term_palette[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff
};

static void term_clear_cells(term_t *t, int from, int to)
{
    int i;
    for (i = from; i < to; ++i)
    {
        t->cells[i].cp = ' ';
        t->cells[i].fg = t->fg;
        t->cells[i].bg = t->bg;
    }
}

/* the grid for a canvas of w x h pixels; what was there is kept */
static void term_size(term_t *t, int w, int h)
{
    int cols = w / t->cell_w, rows = h / t->cell_h;
    term_cell_t *cells;
    int r, c, shift;
    if (cols < 10) cols = 10;
    if (rows < 3) rows = 3;
    if (cols == t->cols && rows == t->rows && t->cells != NULL)
        return;
    cells = malloc((size_t)cols * rows * sizeof(*cells));
    if (cells == NULL)
        return;
    /* the cursor's row stays in the grid: the top rows go when it shrinks */
    shift = t->cells != NULL && t->cy + 1 > rows ? t->cy + 1 - rows : 0;
    for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c)
        {
            term_cell_t *d = &cells[r * cols + c];
            int sr = r + shift;
            if (t->cells != NULL && sr < t->rows && c < t->cols)
                *d = t->cells[sr * t->cols + c];
            else
            {
                d->cp = ' ';
                d->fg = t->fg;
                d->bg = t->bg;
            }
        }
    t->cy -= shift;
    free(t->cells);
    t->cells = cells;
    t->cols = cols;
    t->rows = rows;
    if (t->cx >= cols) t->cx = cols - 1;
    if (t->cy >= rows) t->cy = rows - 1;
    t->dirty = 1;
}

static void term_scroll(term_t *t)
{
    memmove(t->cells, t->cells + t->cols, (size_t)t->cols * (t->rows - 1) * sizeof(term_cell_t));
    term_clear_cells(t, t->cols * (t->rows - 1), t->cols * t->rows);
}

static void term_newline(term_t *t)
{
    t->wrap_next = 0;
    if (t->cy + 1 >= t->rows)
        term_scroll(t);
    else
        ++t->cy;
}

static void term_put(term_t *t, uint32_t cp)
{
    term_cell_t *cell;
    if (t->wrap_next)
    {
        t->cx = 0;
        term_newline(t);
    }
    cell = &t->cells[t->cy * t->cols + t->cx];
    cell->cp = cp;
    cell->fg = t->reverse ? t->bg : (unsigned char)(t->fg | (t->bold && t->fg < 8 ? 8 : 0));
    cell->bg = t->reverse ? t->fg : t->bg;
    if (t->cx + 1 >= t->cols)
        t->wrap_next = 1;
    else
        ++t->cx;
}

static int term_param(term_t *t, int i, int def)
{
    return i < t->nparams && t->params[i] > 0 ? t->params[i] : def;
}

static void term_sgr(term_t *t)
{
    int i;
    if (t->nparams == 0)
        t->nparams = 1, t->params[0] = 0;
    for (i = 0; i < t->nparams; ++i)
    {
        int p = t->params[i];
        if (p == 0)
        {
            t->fg = 7;
            t->bg = 0;
            t->bold = 0;
            t->reverse = 0;
        }
        else if (p == 1)
            t->bold = 1;
        else if (p == 22)
            t->bold = 0;
        else if (p == 7)
            t->reverse = 1;
        else if (p == 27)
            t->reverse = 0;
        else if (p >= 30 && p <= 37)
            t->fg = (unsigned char)(p - 30);
        else if (p == 39)
            t->fg = 7;
        else if (p >= 40 && p <= 47)
            t->bg = (unsigned char)(p - 40);
        else if (p == 49)
            t->bg = 0;
        else if (p >= 90 && p <= 97)
            t->fg = (unsigned char)(p - 90 + 8);
        else if (p >= 100 && p <= 107)
            t->bg = (unsigned char)(p - 100 + 8);
    }
}

static void term_csi(term_t *t, int final)
{
    int n = term_param(t, 0, 1);
    int row = t->cy * t->cols;
    t->wrap_next = 0;
    switch (final)
    {
    case 'A': t->cy -= n; break;
    case 'B': t->cy += n; break;
    case 'C': t->cx += n; break;
    case 'D': t->cx -= n; break;
    case 'G': t->cx = n - 1; break;
    case 'd': t->cy = n - 1; break;
    case 'H':
    case 'f':
        t->cy = term_param(t, 0, 1) - 1;
        t->cx = term_param(t, 1, 1) - 1;
        break;
    case 'J':
        {
            int mode = t->nparams > 0 ? t->params[0] : 0;
            int at = row + t->cx;
            if (mode == 0)
                term_clear_cells(t, at, t->cols * t->rows);
            else if (mode == 1)
                term_clear_cells(t, 0, at + 1);
            else
                term_clear_cells(t, 0, t->cols * t->rows);
        }
        break;
    case 'K':
        {
            int mode = t->nparams > 0 ? t->params[0] : 0;
            if (mode == 0)
                term_clear_cells(t, row + t->cx, row + t->cols);
            else if (mode == 1)
                term_clear_cells(t, row, row + t->cx + 1);
            else
                term_clear_cells(t, row, row + t->cols);
        }
        break;
    case 'm': term_sgr(t); break;
    case 's': t->saved_x = t->cx; t->saved_y = t->cy; break;
    case 'u': t->cx = t->saved_x; t->cy = t->saved_y; break;
    case 'h':
    case 'l':
        if (t->private_mode && term_param(t, 0, 0) == 25)
            t->cursor_on = final == 'h';
        break;
    default:
        break;
    }
    if (t->cx < 0) t->cx = 0;
    if (t->cy < 0) t->cy = 0;
    if (t->cx >= t->cols) t->cx = t->cols - 1;
    if (t->cy >= t->rows) t->cy = t->rows - 1;
}

/* a byte from the console */
static void term_byte(term_t *t, unsigned char b)
{
    t->dirty = 1;
    if (t->state == 1)
    {
        if (b == '[')
        {
            t->state = 2;
            t->nparams = 0;
            t->private_mode = 0;
            memset(t->params, 0, sizeof(t->params));
            return;
        }
        t->state = 0;
        if (b == 'c')
        {
            t->fg = 7;
            t->bg = 0;
            term_clear_cells(t, 0, t->cols * t->rows);
            t->cx = t->cy = 0;
        }
        return;
    }
    if (t->state == 2)
    {
        if (b == '?')
            t->private_mode = 1;
        else if (b >= '0' && b <= '9')
        {
            if (t->nparams == 0)
                t->nparams = 1;
            t->params[t->nparams - 1] = t->params[t->nparams - 1] * 10 + (b - '0');
        }
        else if (b == ';')
        {
            if (t->nparams == 0)
                t->nparams = 1;
            if (t->nparams < 8)
                t->params[t->nparams++] = 0;
        }
        else if (b >= 0x40 && b <= 0x7e)
        {
            t->state = 0;
            term_csi(t, b);
        }
        return;
    }
    if (t->utf8_left > 0)
    {
        if ((b & 0xc0) == 0x80)
        {
            t->utf8 = (t->utf8 << 6) | (b & 0x3f);
            if (--t->utf8_left == 0)
                term_put(t, t->utf8);
            return;
        }
        t->utf8_left = 0;
        term_put(t, 0xfffd);
    }
    if (b == 27)
        t->state = 1;
    else if (b == '\n')
        term_newline(t);
    else if (b == '\r')
    {
        t->cx = 0;
        t->wrap_next = 0;
    }
    else if (b == 8)
    {
        if (t->wrap_next)
            t->wrap_next = 0;
        else if (t->cx > 0)
            --t->cx;
    }
    else if (b == '\t')
    {
        int next = (t->cx / 8 + 1) * 8;
        while (t->cx < next && t->cx < t->cols - 1)
            term_put(t, ' ');
    }
    else if (b == 7 || b < 32)
        ;
    else if (b < 0x80)
        term_put(t, b);
    else if ((b & 0xe0) == 0xc0)
        t->utf8 = b & 0x1f, t->utf8_left = 1;
    else if ((b & 0xf0) == 0xe0)
        t->utf8 = b & 0x0f, t->utf8_left = 2;
    else if ((b & 0xf8) == 0xf0)
        t->utf8 = b & 0x07, t->utf8_left = 3;
    else
        term_put(t, 0xfffd);
}

static void term_queue(term_t *t, const char *bytes)
{
    for (; *bytes != '\0'; ++bytes)
    {
        int next = (t->in_tail + 1) % TERM_INPUT;
        if (next == t->in_head)
            return;
        t->input[t->in_tail] = (unsigned char)*bytes;
        t->in_tail = next;
    }
}

static term_t *term_of_window(GLFWwindow *w)
{
    int i;
    for (i = 0; i < TERM_MAX; ++i)
    {
        petersburg_widget_t *win;
        if (!terms[i].used || terms[i].closed)
            continue;
        win = petersburg_find(terms[i].window);
        if (win != NULL && win->native_window == w)
            return &terms[i];
    }
    return NULL;
}

/* glfw.c's key filter: a terminal's window types into the terminal */
static int term_key(GLFWwindow *w, int key, int action, int mods, unsigned ch)
{
    term_t *t = term_of_window(w);
    char out[8];
    const char *seq = NULL;
    int raw;

    if (t == NULL || t != term_active)
        return 0;
    if (action == GLFW_RELEASE)
        return 1;
    raw = moscow_term_raw() != 0;
    if ((mods & GLFW_MOD_CONTROL) && key >= 'A' && key <= 'Z')
    {
        out[0] = (char)(key - 'A' + 1);
        out[1] = '\0';
        term_queue(t, out);
        return 1;
    }
    switch (key)
    {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: seq = "\r"; break;
    case GLFW_KEY_BACKSPACE: seq = "\x7f"; break;
    case GLFW_KEY_TAB: seq = "\t"; break;
    case GLFW_KEY_ESCAPE: seq = "\x1b"; break;
    case GLFW_KEY_UP: seq = raw ? "\x1b[A" : ""; break;
    case GLFW_KEY_DOWN: seq = raw ? "\x1b[B" : ""; break;
    case GLFW_KEY_RIGHT: seq = raw ? "\x1b[C" : ""; break;
    case GLFW_KEY_LEFT: seq = raw ? "\x1b[D" : ""; break;
    case GLFW_KEY_HOME: seq = raw ? "\x1b[H" : ""; break;
    case GLFW_KEY_END: seq = raw ? "\x1b[F" : ""; break;
    case GLFW_KEY_INSERT: seq = raw ? "\x1b[2~" : ""; break;
    case GLFW_KEY_DELETE: seq = raw ? "\x1b[3~" : ""; break;
    case GLFW_KEY_PAGE_UP: seq = raw ? "\x1b[5~" : ""; break;
    case GLFW_KEY_PAGE_DOWN: seq = raw ? "\x1b[6~" : ""; break;
    default: break;
    }
    if (seq != NULL)
        term_queue(t, seq);
    else if (ch != 0)
    {
        if (ch < 0x80)
            out[0] = (char)ch, out[1] = '\0';
        else if (ch < 0x800)
            out[0] = (char)(0xc0 | (ch >> 6)), out[1] = (char)(0x80 | (ch & 0x3f)), out[2] = '\0';
        else
            out[0] = (char)(0xe0 | (ch >> 12)), out[1] = (char)(0x80 | ((ch >> 6) & 0x3f)),
            out[2] = (char)(0x80 | (ch & 0x3f)), out[3] = '\0';
        term_queue(t, out);
    }
    return 1;
}

static term_glyph_t *term_glyph(term_t *t, uint32_t cp)
{
    ttf_font_t *font = petersburg_font(t->font);
    term_glyph_t *g = &t->glyphs[cp % TERM_GLYPHS];
    if (g->alpha != NULL && g->cp == cp)
        return g;
    free(g->alpha);
    g->alpha = NULL;
    g->cp = cp;
    if (font == NULL || cp == ' ')
        return g;
    g->alpha = ttf_render_glyph(font, ttf_glyph_index(font, cp), (float)t->px,
                                &g->width, &g->height, &g->left, &g->top);
    return g;
}

/* the grid onto its canvas, in the window's pixels */
static void term_draw(term_t *t, GLFWwindow *native)
{
    petersburg_widget_t *canvas = petersburg_find(t->canvas);
    int ox, oy, r, c;
    if (canvas == NULL || native == NULL || canvas->width <= 0)
        return;
    term_size(t, canvas->width, canvas->height);
    ox = canvas->x + (canvas->width - t->cols * t->cell_w) / 2;
    oy = canvas->y + (canvas->height - t->rows * t->cell_h) / 2;
    /* the canvas edges the grid leaves, in the background colour */
    {
        int y;
        for (y = canvas->y; y < canvas->y + canvas->height && y < native->height; ++y)
        {
            uint32_t *row = native->pixels + (size_t)y * native->width;
            int x;
            for (x = canvas->x; x < canvas->x + canvas->width && x < native->width; ++x)
                row[x] = term_palette[0];
        }
    }
    for (r = 0; r < t->rows; ++r)
        for (c = 0; c < t->cols; ++c)
        {
            term_cell_t *cell = &t->cells[r * t->cols + c];
            int cursor = t->cursor_on && r == t->cy && c == t->cx && term_active == t &&
                         glfw.focused == native;
            uint32_t fg = term_palette[cell->fg & 15], bg = term_palette[cell->bg & 15];
            int x0 = ox + c * t->cell_w, y0 = oy + r * t->cell_h, y, x;
            term_glyph_t *g;
            if (cursor)
            {
                uint32_t s = fg;
                fg = bg;
                bg = s == bg ? 0xaaaaaa : s;
            }
            for (y = y0; y < y0 + t->cell_h; ++y)
            {
                uint32_t *row;
                if (y < 0 || y >= native->height)
                    continue;
                row = native->pixels + (size_t)y * native->width;
                for (x = x0; x < x0 + t->cell_w && x < native->width; ++x)
                    row[x] = bg;
            }
            if (cell->cp == ' ' || cell->cp == 0)
                continue;
            g = term_glyph(t, cell->cp);
            if (g->alpha == NULL)
                continue;
            for (y = 0; y < g->height; ++y)
            {
                int sy = y0 + t->ascent + g->top + y;
                uint32_t *row;
                if (sy < y0 || sy >= y0 + t->cell_h || sy >= native->height)
                    continue;
                row = native->pixels + (size_t)sy * native->width;
                for (x = 0; x < g->width; ++x)
                {
                    int sx = x0 + g->left + x;
                    unsigned a = g->alpha[y * g->width + x];
                    if (a == 0 || sx < x0 || sx >= native->width)
                        continue;
                    row[sx] = a == 255 ? fg : gl_mix(row[sx], fg, a);
                }
            }
        }
    t->dirty = 0;
}

/* --- the kernel's side */

/* a terminal on canvas with the font at path (handed over first), px
 * pixels per em: its handle, or -1 */
long gui_term_open(long canvas, const char *font_path, long px)
{
    petersburg_widget_t *widget = petersburg_find(canvas);
    ttf_font_t *font;
    term_t *t = NULL;
    int i;

    if (widget == NULL || widget->type != PETERSBURG_CANVAS)
        return -1;
    for (i = 0; i < TERM_MAX; ++i)
        if (!terms[i].used)
        {
            t = &terms[i];
            break;
        }
    if (t == NULL)
        return -1;
    memset(t, 0, sizeof(*t));
    t->font = petersburg_font_load(font_path);
    font = petersburg_font(t->font);
    if (font == NULL)
        return -1;
    t->used = 1;
    t->canvas = canvas;
    t->window = widget->parent_id;
    t->px = (int)px;
    t->cell_w = (int)(ttf_advance(font, ttf_glyph_index(font, 'M')) * px / font->units_per_em + 0.5);
    t->cell_h = (int)((font->ascent - font->descent) * px / font->units_per_em + 0.5);
    t->ascent = (int)(font->ascent * px / font->units_per_em + 0.5);
    if (t->cell_w < 4) t->cell_w = 4;
    if (t->cell_h < 6) t->cell_h = 6;
    t->fg = 7;
    t->bg = 0;
    t->cursor_on = 1;
    term_size(t, widget->width > 0 ? widget->width : 640, widget->height > 0 ? widget->height : 400);
    glfw_key_filter = term_key;
    return i;
}

/* the console goes to terminal h (or nowhere, -1) */
long gui_term_attach(long h)
{
    term_active = h >= 0 && h < TERM_MAX && terms[h].used ? &terms[h] : NULL;
    return 0;
}

long gui_term_close(long h)
{
    int i;
    if (h < 0 || h >= TERM_MAX || !terms[h].used)
        return -1;
    if (term_active == &terms[h])
        term_active = NULL;
    free(terms[h].cells);
    for (i = 0; i < TERM_GLYPHS; ++i)
        free(terms[h].glyphs[i].alpha);
    terms[h].used = 0;
    return 0;
}

/* the console's bytes, from the kernel's print */
void gui_con_write(long byte)
{
    if (term_active != NULL)
        term_byte(term_active, (unsigned char)byte);
}

/* rows << 16 | columns of the console's terminal, 0 when there is none */
long gui_con_size(void)
{
    if (term_active == NULL)
        return 0;
    return ((long)term_active->rows << 16) | term_active->cols;
}
