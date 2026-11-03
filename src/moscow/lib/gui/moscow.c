/* moscow.c - /lib/gui.so's face to the kernel (src/moscow/gui.su).
 *
 * gui_call runs a Petersburg native with its arguments as c_call passes
 * them (integers, and strings as pointers) and a string of their kinds;
 * the result waits in gui_result_*. Every widget belongs to the program
 * that made it (gui_owner: the depth of programs running inside one
 * another, as Kremlin starts Mbltishi), gui_events hands a program only
 * its own events, and gui_end forgets a program's windows when it ends.
 * gui_events also keeps the screen alive: input, windows closed by their
 * box, and drawing what changed, at most every 30 ms. */

#define GUI_OWNED 4096
#define GUI_DEPTHS 16

static struct
{
    long ids[GUI_OWNED];
    unsigned char owners[GUI_OWNED];
    int count;
} gui_owned;

static long gui_owner_now;
static unsigned char gui_run_requested[GUI_DEPTHS];
static int gui_changed;
static long gui_last_render;
static ussr_value_t gui_result;
static char *gui_events_text;
static size_t gui_events_size;

static long gui_owner_of(long id)
{
    int i;
    for (i = gui_owned.count - 1; i >= 0; --i)
        if (gui_owned.ids[i] == id)
            return gui_owned.owners[i];
    return -1;
}

static void gui_own(long id)
{
    int i;
    /* forget widgets gone */
    if (gui_owned.count == GUI_OWNED)
    {
        int kept = 0;
        for (i = 0; i < gui_owned.count; ++i)
            if (petersburg_find(gui_owned.ids[i]) != NULL)
            {
                gui_owned.ids[kept] = gui_owned.ids[i];
                gui_owned.owners[kept] = gui_owned.owners[i];
                ++kept;
            }
        gui_owned.count = kept;
        if (kept == GUI_OWNED)
            return;
    }
    gui_owned.ids[gui_owned.count] = id;
    gui_owned.owners[gui_owned.count] = (unsigned char)gui_owner_now;
    ++gui_owned.count;
}

/* --- window titles in a TrueType font: Pobeda, as the kernel hands it */

#define GUI_TITLE_PX 14
#define GUI_TITLE_GLYPHS 256

typedef struct
{
    uint32_t cp;
    int done;
    unsigned char *alpha;
    int width, height, left, top;
    double advance;
} gui_title_glyph_t;

static long gui_title_handle;
static gui_title_glyph_t gui_title_glyphs[GUI_TITLE_GLYPHS];

static gui_title_glyph_t *gui_title_glyph(ttf_font_t *font, uint32_t cp)
{
    gui_title_glyph_t *g = &gui_title_glyphs[cp % GUI_TITLE_GLYPHS];
    int glyph;
    if (g->done && g->cp == cp)
        return g;
    free(g->alpha);
    memset(g, 0, sizeof(*g));
    g->cp = cp;
    g->done = 1;
    glyph = ttf_glyph_index(font, cp);
    g->advance = ttf_advance(font, glyph) * (double)GUI_TITLE_PX / font->units_per_em;
    g->alpha = ttf_render_glyph(font, glyph, (float)GUI_TITLE_PX, &g->width, &g->height,
                                &g->left, &g->top);
    return g;
}

/* UTF-8 text onto the screen being composed, its line box from y */
static int gui_title_text(int x, int y, const char *text, int max_width, uint32_t color)
{
    ttf_font_t *font = petersburg_font(gui_title_handle);
    const unsigned char *s = (const unsigned char *)text;
    double pen = x;
    int baseline;
    if (font == NULL)
        return 0;
    baseline = y + (int)(font->ascent * (double)GUI_TITLE_PX / font->units_per_em + 0.5);
    while (*s != '\0')
    {
        gui_title_glyph_t *g = gui_title_glyph(font, ttf_utf8_next(&s));
        int gx = (int)(pen + 0.5) + g->left, gy = baseline + g->top, r, c;
        if (pen + g->advance > x + max_width)
            break;
        for (r = 0; g->alpha != NULL && r < g->height; ++r)
        {
            int sy = gy + r;
            if (sy < 0 || sy >= glfw.height)
                continue;
            for (c = 0; c < g->width; ++c)
            {
                int sx = gx + c;
                unsigned a = g->alpha[r * g->width + c];
                uint32_t *p;
                if (a == 0 || sx < 0 || sx >= glfw.width)
                    continue;
                p = &glfw.screen[(size_t)sy * glfw.width + sx];
                *p = a == 255 ? color : gl_mix(*p, color, a);
            }
        }
        pen += g->advance;
    }
    return 1;
}

/* the font for window titles, handed over first; 0, or -1 */
long gui_title_font(const char *path)
{
    gui_title_handle = petersburg_font_load(path);
    if (gui_title_handle == 0)
        return -1;
    glfw_title_draw = gui_title_text;
    glfw.dirty = 1;
    return 0;
}

/* the heap, from the kernel at boot */
long gui_start(void *arena, long size)
{
    heap_init(arena, (size_t)size);
    return 0;
}

long gui_owner(long depth)
{
    gui_owner_now = depth < 0 ? 0 : depth >= GUI_DEPTHS ? GUI_DEPTHS - 1 : depth;
    return 0;
}

/* the windows there are, of every program */
long gui_windows(void)
{
    petersburg_widget_t *w;
    long n = 0;
    for (w = petersburg_widgets; w != NULL; w = w->next)
        if (w->type == PETERSBURG_WINDOW)
            ++n;
    return n;
}

static void gui_render(void)
{
    petersburg_widget_t *w;
    int i;
    for (w = petersburg_widgets; w != NULL; w = w->next)
    {
        if (w->type != PETERSBURG_WINDOW || w->native_window == NULL ||
            !w->native_window->visible)
            continue;
        petersburg_render_window(w);
        for (i = 0; i < TERM_MAX; ++i)
            if (terms[i].used && !terms[i].closed && terms[i].window == w->id)
                term_draw(&terms[i], w->native_window);
    }
    gui_changed = 0;
    gui_last_render = moscow_ticks();
}

/* input, windows closed, and the screen when something changed */
long gui_pump(void)
{
    petersburg_widget_t *w;
    int i;
    unsigned before = petersburg_event_count;

    if (!glfw.on)
        return 0;
    glfw_input();
    if (petersburg_event_count != before)
        gui_changed = 1;
    for (;;)
    {
        long closing = 0;
        for (w = petersburg_widgets; w != NULL; w = w->next)
            if (w->type == PETERSBURG_WINDOW && w->native_window != NULL &&
                glfwWindowShouldClose(w->native_window))
            {
                closing = w->id;
                break;
            }
        if (closing == 0)
            break;
        for (i = 0; i < TERM_MAX; ++i)
            if (terms[i].used && terms[i].window == closing)
            {
                /* the shell in it reads the end of its input */
                terms[i].closed = 1;
                term_queue(&terms[i], "\x04");
            }
        (void)petersburg_destroy(closing);
        gui_changed = 1;
    }
    for (i = 0; i < TERM_MAX; ++i)
        if (terms[i].used && terms[i].dirty)
            gui_changed = 1;
    if (gui_changed && moscow_ticks() - gui_last_render >= 3)
        gui_render();
    glfw_present();
    return 0;
}

static int gui_is_query(const char *name)
{
    return strncmp(name, "petersburg_get_", 15) == 0 ||
           strcmp(name, "petersburg_alive") == 0 ||
           strcmp(name, "petersburg_canvas_count") == 0 ||
           strncmp(name, "petersburg_font_", 16) == 0;
}

/* a native: name, kinds ("i" integer, "s" string, "n" number as text,
 * "b" boolean, "z" null) and up to seven arguments. 1 done, -1 failed */
long gui_call(const char *name, const char *kinds, long a0, long a1, long a2,
              long a3, long a4, long a5, long a6)
{
    long raw[7] = { a0, a1, a2, a3, a4, a5, a6 };
    ussr_value_t values[7];
    size_t n = strlen(kinds);
    size_t i;
    int status;

    if (n > 7)
        return -1;
    for (i = 0; i < n; ++i)
    {
        switch (kinds[i])
        {
        case 'i':
            values[i] = ussr_integer(raw[i]);
            break;
        case 's':
            values[i].type = USSR_STRING;
            values[i].data.string = (char *)raw[i];
            break;
        case 'n':
            values[i].type = USSR_REAL;
            values[i].data.real = strtod((const char *)raw[i], NULL);
            break;
        case 'b':
            values[i] = ussr_boolean(raw[i] != 0);
            break;
        default:
            values[i].type = USSR_NULL;
            break;
        }
    }
    ussr_value_free(&gui_result);
    gui_result.type = USSR_NULL;
    status = ussr_petersburg_dispatch(NULL, name, values, n, &gui_result);
    if (status > 0 && strncmp(name, "petersburg_create_", 18) == 0 &&
        gui_result.type == USSR_INTEGER)
        gui_own((long)gui_result.data.integer);
    if (status > 0 && (strcmp(name, "petersburg_run") == 0 ||
                       strcmp(name, "petersburg_show") == 0))
        gui_run_requested[gui_owner_now] = 1;
    if (!gui_is_query(name))
        gui_changed = 1;
    petersburg_last_error = 0;
    return status > 0 ? 1 : -1;
}

/* the result: 'i' integer, 'b' boolean, 's' string, 'n' number, 'z' */
long gui_result_kind(void)
{
    switch (gui_result.type)
    {
    case USSR_INTEGER: return 'i';
    case USSR_BOOLEAN: return 'b';
    case USSR_STRING: return 's';
    case USSR_REAL: return 'n';
    default: return 'z';
    }
}

long gui_result_int(void)
{
    if (gui_result.type == USSR_BOOLEAN)
        return gui_result.data.boolean;
    if (gui_result.type == USSR_REAL)
        return (long)gui_result.data.real;
    return gui_result.type == USSR_INTEGER ? (long)gui_result.data.integer : 0;
}

const char *gui_result_text(void)
{
    return gui_result.type == USSR_STRING && gui_result.data.string != NULL
               ? gui_result.data.string : "";
}

/* what stderr said since the last time, then forgotten */
const char *gui_message(void)
{
    static char said[sizeof(gui_messages)];
    memcpy(said, gui_messages, sizeof(said));
    gui_messages[0] = '\0';
    return said;
}

static int gui_append(size_t *len, const char *text)
{
    size_t n = strlen(text);
    if (*len + n + 1 > gui_events_size)
    {
        size_t size = (gui_events_size ? gui_events_size * 2 : 1024) + n;
        char *grown = realloc(gui_events_text, size);
        if (grown == NULL)
            return 0;
        gui_events_text = grown;
        gui_events_size = size;
    }
    memcpy(gui_events_text + *len, text, n + 1);
    *len += n;
    return 1;
}

/* petersburg_take_events for the program now running, as UNO text: 1
 * while it has a Petersburg run going (a window of its own left), then
 * its events [callback, widget, values...]; the others' are dropped */
const char *gui_events(void)
{
    petersburg_widget_t *w;
    size_t len = 0;
    size_t i;
    int running = 0;
    char item[160];

    gui_pump();
    if (gui_run_requested[gui_owner_now])
        for (w = petersburg_widgets; w != NULL; w = w->next)
            if (w->type == PETERSBURG_WINDOW && gui_owner_of(w->id) == gui_owner_now)
                running = 1;
    if (!running)
        gui_run_requested[gui_owner_now] = 0;
    snprintf(item, sizeof(item), "[%d", running);
    gui_append(&len, item);
    for (i = 0; i < petersburg_event_count; ++i)
    {
        const petersburg_event_t *e = &petersburg_events[
            (petersburg_event_head + i) %
            (sizeof(petersburg_events) / sizeof(petersburg_events[0]))];
        size_t k;
        if (gui_owner_of(e->widget_id) != gui_owner_now)
            continue;
        snprintf(item, sizeof(item), ",[`%s`,%ld", e->callback, e->widget_id);
        gui_append(&len, item);
        for (k = 0; k < e->value_count; ++k)
        {
            snprintf(item, sizeof(item), ",%ld", e->values[k]);
            gui_append(&len, item);
        }
        gui_append(&len, "]");
    }
    gui_append(&len, "]");
    petersburg_event_head = 0;
    petersburg_event_count = 0;
    return gui_events_text;
}

/* a program ended: its windows (with all in them) go */
long gui_end(long depth)
{
    petersburg_widget_t *w;
    int again = 1;
    while (again)
    {
        again = 0;
        for (w = petersburg_widgets; w != NULL; w = w->next)
            if (w->type == PETERSBURG_WINDOW && gui_owner_of(w->id) >= depth)
            {
                int i;
                for (i = 0; i < TERM_MAX; ++i)
                    if (terms[i].used && terms[i].window == w->id)
                        gui_term_close(i);
                (void)petersburg_destroy(w->id);
                again = 1;
                break;
            }
    }
    if (depth >= 0 && depth < GUI_DEPTHS)
        for (; depth < GUI_DEPTHS; ++depth)
            gui_run_requested[depth] = 0;
    gui_changed = 1;
    gui_pump();
    return gui_windows();
}

/* a console byte for the terminal, -1 when none has been typed; keeps
 * the screen going meanwhile */
long gui_con_read(void)
{
    term_t *t = term_active;
    int c;
    gui_pump();
    if (t == NULL || t->in_head == t->in_tail)
        return -1;
    c = t->input[t->in_head];
    t->in_head = (t->in_head + 1) % TERM_INPUT;
    return c;
}
