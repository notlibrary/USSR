#include "petersburg.h"
#include "eventq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USSR_WITH_GLFW
#include <GLFW/glfw3.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "ttf.h"

#define PETERSBURG_MAX_SHAPES 200000

typedef enum
{
    PETERSBURG_WINDOW,
    PETERSBURG_LABEL,
    PETERSBURG_BUTTON,
    PETERSBURG_TEXTBOX,
    PETERSBURG_CHECKBOX,
    PETERSBURG_LIST,
    PETERSBURG_COMBOBOX,
    PETERSBURG_IMAGE,
    PETERSBURG_CANVAS,
    PETERSBURG_TOOLBAR
} petersburg_type_t;

typedef enum
{
    PETERSBURG_SHAPE_LINE,
    PETERSBURG_SHAPE_CIRCLE,
    PETERSBURG_SHAPE_ARC,
    PETERSBURG_SHAPE_RECT,
    PETERSBURG_SHAPE_FILL,
    PETERSBURG_SHAPE_TEXT,
    PETERSBURG_SHAPE_FONT_TEXT /* v[2] pixels per em, v[3] font handle */
} petersburg_shape_kind_t;

/* One retained canvas primitive, in canvas-local framebuffer pixels. */
typedef struct
{
    petersburg_shape_kind_t kind;
    double v[6];
    unsigned long color;
    char *text;
} petersburg_shape_t;

typedef struct petersburg_widget
{
    long id;
    long parent_id;
    petersburg_type_t type;
    char *text;
    char *click_callback;
    char *change_callback;
    char *key_callback;
    char *keyup_callback;
    char *focus_callback;
    char *wheel_callback;
    char *menu_callback;
    char *double_callback;
    char *char_callback;
    int x;
    int y;
    int width;
    int height;
    int preferred_height;
    int laid_out;
    int expand;
    int grid_row;
    int grid_column;
    int in_grid;
    int scroll;
    int checked;
    int selected;
    unsigned char *pixels;
    GLFWwindow *native_window;
    int image_width;
    int image_height;
    unsigned char *marks;
    size_t mark_count;
    petersburg_shape_t *shapes;
    size_t shape_count;
    size_t shape_capacity;
    unsigned long background;
    int edge_to_edge; /* a fullscreen window: no margin around widgets */
    struct petersburg_widget *next;
} petersburg_widget_t;

/* A queued callback: the widget handle plus up to three integer values. */
typedef struct
{
    long widget_id;
    size_t value_count;
    long values[3];
    char callback[128];
} petersburg_event_t;

static petersburg_widget_t *petersburg_widgets;
static long petersburg_next_id = 1;
static int petersburg_initialized;
static int petersburg_run_requested;
static int petersburg_last_error;
static petersburg_event_t petersburg_events[64];
static size_t petersburg_event_head;
static size_t petersburg_event_count;
static petersburg_widget_t *petersburg_focus;

static char *petersburg_strdup(const char *value)
{
    size_t size;
    char *copy;

    if (value == NULL)
        value = "";
    size = strlen(value) + 1;
    copy = malloc(size);
    if (copy != NULL)
        memcpy(copy, value, size);
    return copy;
}

static petersburg_widget_t *petersburg_find(long id)
{
    petersburg_widget_t *widget;

    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
        if (widget->id == id)
            return widget;
    return NULL;
}

static int petersburg_expect_integer(
    const ussr_value_t *value,
    size_t index,
    long *out
)
{
    if (value == NULL)
        return 0;
    if (value[index].type != USSR_INTEGER ||
        value[index].data.integer < LONG_MIN ||
        value[index].data.integer > LONG_MAX)
        return 0;
    *out = (long)value[index].data.integer;
    return 1;
}
static int petersburg_expect_string(
    const ussr_value_t *value,
    size_t index,
    const char **out
)
{
    if (value == NULL || value[index].type != USSR_STRING)
        return 0;
    *out = value[index].data.string;
    return *out != NULL;
}

static int petersburg_expect_number(
    const ussr_value_t *value,
    size_t index,
    double *out
)
{
    if (value == NULL)
        return 0;
    if (value[index].type == USSR_INTEGER)
        *out = (double)value[index].data.integer;
    else if (value[index].type == USSR_REAL)
        *out = value[index].data.real;
    else
        return 0;
    return isfinite(*out);
}

static int petersburg_arity(
    const char *name,
    size_t actual,
    size_t expected
)
{
    if (actual == expected)
        return 1;
    fprintf(stderr, "USSR: %s expects %lu argument(s), got %lu\n",
            name, (unsigned long)expected, (unsigned long)actual);
    return 0;
}

static int petersburg_initialize(void)
{
    if (petersburg_initialized)
        return 1;
    if (!glfwInit())
    {
        fprintf(stderr, "USSR: Petersburg could not initialize GLFW\n");
        return 0;
    }
    petersburg_initialized = 1;
    return 1;
}

static petersburg_widget_t *petersburg_add(
    petersburg_type_t type,
    long parent_id,
    const char *text
)
{
    petersburg_widget_t *widget;

    widget = calloc(1, sizeof(*widget));
    if (widget == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not allocate a widget\n");
        return NULL;
    }
    widget->text = petersburg_strdup(text);
    if (widget->text == NULL)
    {
        free(widget);
        return NULL;
    }
    widget->id = petersburg_next_id++;
    widget->parent_id = parent_id;
    widget->type = type;
    widget->grid_row = -1;
    widget->grid_column = -1;
    widget->preferred_height = type == PETERSBURG_LABEL ? 28 :
                               type == PETERSBURG_TEXTBOX ? 34 :
                               type == PETERSBURG_LIST ? 112 :
                               type == PETERSBURG_IMAGE ? 120 :
                               type == PETERSBURG_CANVAS ? 240 : 36;
    widget->height = widget->preferred_height;
    if (petersburg_widgets == NULL)
        petersburg_widgets = widget;
    else
    {
        petersburg_widget_t *last = petersburg_widgets;
        while (last->next != NULL)
            last = last->next;
        last->next = widget;
    }
    return widget;
}

static void petersburg_queue(
    const petersburg_widget_t *widget,
    const char *callback,
    size_t value_count,
    long first,
    long second,
    long third
)
{
    petersburg_event_t *event;

    if (widget == NULL || callback == NULL || callback[0] == '\0' ||
        petersburg_last_error)
        return;
    if (petersburg_event_count == sizeof(petersburg_events) /
                                 sizeof(petersburg_events[0]))
    {
        fprintf(stderr, "USSR: Petersburg event queue is full\n");
        petersburg_last_error = 1;
        return;
    }
    event = &petersburg_events[
        (petersburg_event_head + petersburg_event_count) %
        (sizeof(petersburg_events) / sizeof(petersburg_events[0]))
    ];
    event->widget_id = widget->id;
    event->value_count = value_count;
    event->values[0] = first;
    event->values[1] = second;
    event->values[2] = third;
    snprintf(event->callback, sizeof(event->callback), "%s", callback);
    ++petersburg_event_count;
}

static void petersburg_queue_event(
    petersburg_widget_t *widget,
    int changed
)
{
    if (widget == NULL)
        return;
    petersburg_queue(widget,
                     changed ? widget->change_callback
                             : widget->click_callback,
                     0, 0, 0, 0);
}

static void petersburg_queue_pointer(
    petersburg_widget_t *widget,
    int x,
    int y
)
{
    if (widget == NULL)
        return;
    petersburg_queue(widget, widget->click_callback, 2, x, y, 0);
}

/* A second left press on the same canvas within this many seconds and
 * pixels of the first is a double click. */
#define PETERSBURG_DOUBLE_SECONDS 0.45
#define PETERSBURG_DOUBLE_PIXELS 5
static long petersburg_last_press_id;
static double petersburg_last_press_time = -10.0;
static int petersburg_last_press_x;
static int petersburg_last_press_y;

/* Left press on a canvas: "pointer" always, then "double" when it is the
 * second press of a double click (a third press starts over). */
static void petersburg_canvas_press(
    petersburg_widget_t *widget,
    int x,
    int y,
    double now
)
{
    petersburg_queue_pointer(widget, x, y);
    if (petersburg_last_press_id == widget->id &&
        now - petersburg_last_press_time <= PETERSBURG_DOUBLE_SECONDS &&
        abs(x - petersburg_last_press_x) <= PETERSBURG_DOUBLE_PIXELS &&
        abs(y - petersburg_last_press_y) <= PETERSBURG_DOUBLE_PIXELS)
    {
        petersburg_queue(widget, widget->double_callback, 2, x, y, 0);
        petersburg_last_press_time = -10.0;
        return;
    }
    petersburg_last_press_id = widget->id;
    petersburg_last_press_time = now;
    petersburg_last_press_x = x;
    petersburg_last_press_y = y;
}

static void petersburg_queue_key(
    petersburg_widget_t *window,
    int key
)
{
    if (window == NULL)
        return;
    petersburg_queue(window, window->key_callback, 1, key, 0, 0);
}

static void petersburg_rect(float x, float y, float width, float height)
{
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

static unsigned char petersburg_glyph(char character, int row)
{
    static const unsigned char glyphs[36][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    unsigned char pattern[7] = {0,0,0,0,0,0,0};
    unsigned int index;

    if (character >= 'a' && character <= 'z')
        character = (char)(character - 'a' + 'A');
    if (character >= '0' && character <= '9')
        index = (unsigned int)(character - '0');
    else if (character >= 'A' && character <= 'Z')
        index = (unsigned int)(character - 'A' + 10);
    else
    {
        switch (character)
        {
        case '.': pattern[6] = 4; break;
        case ',': pattern[5] = 4; pattern[6] = 8; break;
        case ':': pattern[2] = 4; pattern[5] = 4; break;
        case ';': pattern[2] = 4; pattern[5] = 4; pattern[6] = 8; break;
        case '!': pattern[0] = 4; pattern[1] = 4; pattern[2] = 4;
                  pattern[3] = 4; pattern[5] = 4; break;
        case '?': pattern[0] = 14; pattern[1] = 17; pattern[2] = 1;
                  pattern[3] = 2; pattern[4] = 4; pattern[6] = 4; break;
        case '-': pattern[3] = 14; break;
        case '_': pattern[6] = 31; break;
        case '*': pattern[0] = 17; pattern[1] = 10; pattern[2] = 31;
                  pattern[3] = 10; pattern[4] = 17; break;
        case '+': pattern[2] = 4; pattern[3] = 14; pattern[4] = 4; break;
        case '/': pattern[0] = 1; pattern[1] = 2; pattern[2] = 2;
                  pattern[3] = 4; pattern[4] = 8; pattern[5] = 8;
                  pattern[6] = 16; break;
        case '\\': pattern[0] = 16; pattern[1] = 8; pattern[2] = 8;
                   pattern[3] = 4; pattern[4] = 2; pattern[5] = 2;
                   pattern[6] = 1; break;
        case '|': pattern[0] = 4; pattern[1] = 4; pattern[2] = 4;
                  pattern[3] = 4; pattern[4] = 4; pattern[5] = 4;
                  pattern[6] = 4; break;
        case '[': pattern[0] = 14; pattern[1] = 8; pattern[2] = 8;
                  pattern[3] = 8; pattern[4] = 8; pattern[5] = 8;
                  pattern[6] = 14; break;
        case ']': pattern[0] = 14; pattern[1] = 2; pattern[2] = 2;
                  pattern[3] = 2; pattern[4] = 2; pattern[5] = 2;
                  pattern[6] = 14; break;
        case '(': pattern[0] = 2; pattern[1] = 4; pattern[2] = 8;
                  pattern[3] = 8; pattern[4] = 8; pattern[5] = 4;
                  pattern[6] = 2; break;
        case ')': pattern[0] = 8; pattern[1] = 4; pattern[2] = 2;
                  pattern[3] = 2; pattern[4] = 2; pattern[5] = 4;
                  pattern[6] = 8; break;
        case '=': pattern[2] = 14; pattern[4] = 14; break;
        default: break;
        }
        return pattern[row];
    }
    return glyphs[index][row];
}

/* Bitmap text, each font pixel drawn as a scale x scale square. */
static void petersburg_text_scaled(
    float x,
    float y,
    const char *text,
    float red,
    float green,
    float blue,
    int max_width,
    int scale
)
{
    size_t i;
    float k = (float)scale;
    glColor3f(red, green, blue);
    for (i = 0; text != NULL && text[i] != '\0' &&
                (int)(i * 7) * scale < max_width; ++i)
    {
        int row;
        for (row = 0; row < 7; ++row)
        {
            unsigned char bits = petersburg_glyph(text[i], row);
            int column;
            for (column = 0; column < 5; ++column)
            {
                if ((bits & (1u << (4 - column))) != 0)
                    petersburg_rect(x + (float)(i * 7 + column) * k,
                                    y + row * k, k, k);
            }
        }
    }
}

static void petersburg_text(
    float x,
    float y,
    const char *text,
    float red,
    float green,
    float blue,
    int max_width
)
{
    petersburg_text_scaled(x, y, text, red, green, blue, max_width, 1);
}

/* --- outline fonts --------------------------------------------------------
 * loadfont reads a TrueType file once per path; handles index
 * petersburg_fonts from 1. Glyph bitmaps become alpha textures, cached per
 * OpenGL context (each window has its own) and dropped with the window. */
#define PETERSBURG_MAX_FONTS 64
#define PETERSBURG_GLYPH_BUCKETS 1024

typedef struct petersburg_glyph_entry
{
    GLFWwindow *context;
    int font;
    int glyph;
    int px;
    GLuint texture; /* 0: an empty glyph (a space) */
    int width;
    int height;
    int left;
    int top;
    struct petersburg_glyph_entry *next;
} petersburg_glyph_entry_t;

static ttf_font_t petersburg_fonts[PETERSBURG_MAX_FONTS];
static char *petersburg_font_paths[PETERSBURG_MAX_FONTS];
static int petersburg_font_count;
static petersburg_glyph_entry_t *petersburg_glyph_cache[PETERSBURG_GLYPH_BUCKETS];

static ttf_font_t *petersburg_font(long handle)
{
    if (handle < 1 || handle > petersburg_font_count)
        return NULL;
    return &petersburg_fonts[handle - 1];
}

/* Handle of the font at path, loading it the first time; 0 if unusable. */
static long petersburg_font_load(const char *path)
{
    FILE *file;
    long size;
    unsigned char *data;
    int i;

    for (i = 0; i < petersburg_font_count; ++i)
        if (strcmp(petersburg_font_paths[i], path) == 0)
            return i + 1;
    if (petersburg_font_count == PETERSBURG_MAX_FONTS ||
        (file = fopen(path, "rb")) == NULL)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 ||
        size > 64L * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0 ||
        (data = malloc((size_t)size)) == NULL)
    {
        fclose(file);
        return 0;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size ||
        !ttf_load(&petersburg_fonts[petersburg_font_count], data,
                  (size_t)size) ||
        (petersburg_font_paths[petersburg_font_count] =
             petersburg_strdup(path)) == NULL)
    {
        fclose(file);
        free(data);
        return 0;
    }
    fclose(file);
    return ++petersburg_font_count;
}

static double petersburg_font_scale(const ttf_font_t *font, double px)
{
    return px / (double)font->units_per_em;
}

/* Advance of UTF-8 text in pixels at px pixels per em. */
static double petersburg_font_width(const ttf_font_t *font, double px,
                                    const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    long units = 0;

    while (*s != '\0')
        units += ttf_advance(font, ttf_glyph_index(font, ttf_utf8_next(&s)));
    return (double)units * petersburg_font_scale(font, px);
}

static petersburg_glyph_entry_t *petersburg_font_glyph(int font_handle,
                                                       int glyph, int px)
{
    GLFWwindow *context = glfwGetCurrentContext();
    unsigned bucket = ((unsigned)glyph * 31u + (unsigned)px * 131u +
                       (unsigned)font_handle * 7919u) %
                      PETERSBURG_GLYPH_BUCKETS;
    petersburg_glyph_entry_t *entry;
    unsigned char *pixels;

    for (entry = petersburg_glyph_cache[bucket]; entry != NULL;
         entry = entry->next)
        if (entry->context == context && entry->font == font_handle &&
            entry->glyph == glyph && entry->px == px)
            return entry;
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    entry->context = context;
    entry->font = font_handle;
    entry->glyph = glyph;
    entry->px = px;
    pixels = ttf_render_glyph(petersburg_font(font_handle), glyph,
                              (float)px, &entry->width, &entry->height,
                              &entry->left, &entry->top);
    if (pixels != NULL)
    {
        glGenTextures(1, &entry->texture);
        glBindTexture(GL_TEXTURE_2D, entry->texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, entry->width, entry->height,
                     0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);
    }
    entry->next = petersburg_glyph_cache[bucket];
    petersburg_glyph_cache[bucket] = entry;
    return entry;
}

/* Drops the cached glyphs of a window's context (the context frees the
 * textures when it is destroyed). */
static void petersburg_glyphs_forget(GLFWwindow *context)
{
    int bucket;

    for (bucket = 0; bucket < PETERSBURG_GLYPH_BUCKETS; ++bucket)
    {
        petersburg_glyph_entry_t **link = &petersburg_glyph_cache[bucket];
        while (*link != NULL)
        {
            petersburg_glyph_entry_t *entry = *link;
            if (entry->context == context)
            {
                *link = entry->next;
                free(entry);
            }
            else
                link = &entry->next;
        }
    }
}

/* Text whose line box starts at y: the baseline is the font's ascent
 * below it. Pen positions are rounded per glyph so stems stay sharp. */
static void petersburg_font_text(float x, float y, const char *text,
                                 unsigned long color, long font_handle,
                                 double px, float max_x)
{
    ttf_font_t *font = petersburg_font(font_handle);
    const unsigned char *s = (const unsigned char *)text;
    double scale, pen = x;
    float baseline;
    int size = (int)(px + 0.5);

    if (font == NULL || size < 1)
        return;
    scale = petersburg_font_scale(font, size);
    baseline = (float)floor(y + font->ascent * scale + 0.5);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f((float)((color >> 16) & 255) / 255.0f,
              (float)((color >> 8) & 255) / 255.0f,
              (float)(color & 255) / 255.0f);
    while (*s != '\0' && pen < max_x)
    {
        int glyph = ttf_glyph_index(font, ttf_utf8_next(&s));
        petersburg_glyph_entry_t *entry =
            petersburg_font_glyph((int)font_handle, glyph, size);
        if (entry != NULL && entry->texture != 0)
        {
            float gx = (float)floor(pen + 0.5) + (float)entry->left;
            float gy = baseline + (float)entry->top;
            glBindTexture(GL_TEXTURE_2D, entry->texture);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(gx, gy);
            glTexCoord2f(1, 0);
            glVertex2f(gx + (float)entry->width, gy);
            glTexCoord2f(1, 1);
            glVertex2f(gx + (float)entry->width, gy + (float)entry->height);
            glTexCoord2f(0, 1);
            glVertex2f(gx, gy + (float)entry->height);
            glEnd();
        }
        pen += ttf_advance(font, glyph) * scale;
    }
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

static void petersburg_layout_flow(petersburg_widget_t *window, int top)
{
    petersburg_widget_t *widget;
    petersburg_widget_t *grid_widgets[256];
    int grid_rows[256];
    int row_heights[256];
    int row_expand[256];
    int count = 0;
    int preferred = 0;
    int expand_count = 0;
    int grid_count = 0;
    int max_grid_row = -1;
    int has_grid = 0;
    int gap = 8;
    int margin = window->edge_to_edge ? 0 : 16;
    int available;
    int extra;
    int y = top;

    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        if (widget->parent_id == window->id && widget->type != PETERSBURG_WINDOW &&
            widget->type != PETERSBURG_TOOLBAR)
        {
            if (widget->in_grid)
            {
                has_grid = 1;
                if (widget->grid_row >= 0 && widget->grid_row < 256 &&
                    grid_count < 256)
                {
                    grid_widgets[grid_count] = widget;
                    grid_rows[grid_count] = widget->grid_row;
                    if (widget->grid_row > max_grid_row)
                        max_grid_row = widget->grid_row;
                    ++grid_count;
                }
                continue;
            }
            ++count;
            preferred += widget->preferred_height;
            if (widget->expand)
                ++expand_count;
        }
    }

    if (has_grid && max_grid_row >= 0)
    {
        int row;
        int grid_available;
        int grid_preferred = 0;
        int grid_expand = 0;

        for (row = 0; row <= max_grid_row; ++row)
        {
            row_heights[row] = 0;
            row_expand[row] = 0;
        }
        for (count = 0; count < grid_count; ++count)
        {
            widget = grid_widgets[count];
            row = grid_rows[count];
            if (widget->preferred_height > row_heights[row])
                row_heights[row] = widget->preferred_height;
            if (widget->expand)
                row_expand[row] = 1;
        }
        for (row = 0; row <= max_grid_row; ++row)
        {
            grid_preferred += row_heights[row];
            if (row_expand[row])
                ++grid_expand;
        }
        grid_available = window->height - top - margin -
                         gap * max_grid_row;
        extra = grid_available - grid_preferred;
        for (row = 0; row <= max_grid_row; ++row)
        {
            if (extra > 0 && row_expand[row] && grid_expand > 0)
                row_heights[row] += extra / grid_expand;
            for (count = 0; count < grid_count; ++count)
            {
                int column_width;
                if (grid_rows[count] != row)
                    continue;
                widget = grid_widgets[count];
                column_width = (window->width - margin * 2 - gap) / 2;
                widget->x = margin + widget->grid_column *
                                      (column_width + gap);
                widget->y = y;
                widget->width = column_width;
                widget->height = row_heights[row];
            }
            y += row_heights[row] + gap;
        }
        count = 0;
        for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
        {
            if (widget->parent_id == window->id &&
                widget->type != PETERSBURG_WINDOW &&
            widget->type != PETERSBURG_TOOLBAR && !widget->in_grid)
            {
                widget->x = margin;
                widget->y = y;
                widget->width = window->width - margin * 2;
                widget->height = widget->preferred_height;
                y += widget->height + gap;
            }
        }
        return;
    }

    if (count == 0)
        return;
    available = window->height - top - margin - gap * (count - 1);
    extra = available - preferred;
    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        int height;
        if (widget->parent_id != window->id ||
            (widget->type == PETERSBURG_WINDOW ||
             widget->type == PETERSBURG_TOOLBAR))
            continue;
        height = widget->preferred_height;
        if (extra > 0 && expand_count > 0 && widget->expand)
            height += extra / expand_count;
        widget->x = margin;
        widget->y = y;
        widget->width = window->width - margin * 2;
        widget->height = height;
        y += height + gap;
    }
}

/* Toolbars dock at the top of their window in creation order; everything
 * else flows (or sits in the grid) below them. */
static void petersburg_layout(petersburg_widget_t *window)
{
    petersburg_widget_t *widget;
    int margin = window->edge_to_edge ? 0 : 16;
    int gap = 8;
    int top = margin;

    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        if (widget->parent_id != window->id ||
            widget->type != PETERSBURG_TOOLBAR)
            continue;
        widget->x = margin;
        widget->y = top;
        widget->width = window->width - margin * 2;
        widget->height = widget->preferred_height;
        top += widget->height + gap;
    }
    petersburg_layout_flow(window, top);
}

static int petersburg_item_count(const char *items)
{
    int count = 0;
    const char *cursor = items;

    while (cursor != NULL && *cursor != '\0')
    {
        int nonempty = 0;
        while (*cursor != '\0')
        {
            if (*cursor == '|')
                break;
            if (*cursor == '\\' &&
                (cursor[1] == '|' || cursor[1] == '\\'))
            {
                nonempty = 1;
                cursor += 2;
            }
            else
            {
                nonempty = 1;
                ++cursor;
            }
        }
        if (nonempty)
            ++count;
        if (*cursor == '|')
            ++cursor;
    }
    return count;
}

static int petersburg_item_at(
    const petersburg_widget_t *widget,
    int index,
    char *buffer,
    size_t buffer_size
)
{
    const char *cursor = widget->text;
    int item_index = 0;
    size_t length = 0;

    if (cursor == NULL || buffer == NULL || buffer_size == 0 || index < 0)
        return 0;
    while (*cursor != '\0')
    {
        if (*cursor == '|')
        {
            if (length > 0 && item_index == index)
            {
                buffer[length] = '\0';
                return 1;
            }
            if (length > 0)
                ++item_index;
            length = 0;
            if (*cursor == '|')
            {
                ++cursor;
                continue;
            }
            break;
        }
        if (*cursor == '\\' &&
            (cursor[1] == '|' || cursor[1] == '\\'))
        {
            ++cursor;
        }
        if (length + 1 >= buffer_size)
            return 0;
        buffer[length++] = *cursor++;
    }
    if (item_index == index && length > 0)
    {
        buffer[length] = '\0';
        return 1;
    }
    return 0;
}

static const char *petersburg_selected_item(
    const petersburg_widget_t *widget,
    char *buffer,
    size_t buffer_size
)
{
    if (!petersburg_item_at(widget, widget->selected, buffer, buffer_size))
    {
        if (buffer_size > 0)
            buffer[0] = '\0';
    }
    return buffer;
}

static char *petersburg_selected_item_copy(
    const petersburg_widget_t *widget
)
{
    size_t size = widget != NULL && widget->text != NULL
                      ? strlen(widget->text) + 1
                      : 1;
    char *copy = malloc(size);

    if (copy == NULL)
        return NULL;
    if (!petersburg_item_at(widget, widget->selected, copy, size))
        copy[0] = '\0';
    return copy;
}

static void petersburg_color(unsigned long color)
{
    glColor3f((float)((color >> 16) & 255) / 255.0f,
              (float)((color >> 8) & 255) / 255.0f,
              (float)(color & 255) / 255.0f);
}

static int petersburg_arc_segments(double radius, double sweep)
{
    double segments = radius * sweep / 4.0;

    if (segments < 12.0)
        segments = 12.0;
    if (segments > 720.0)
        segments = 720.0;
    return (int)segments;
}

/* Places a toolbar's buttons left to right at their natural width. */
static void petersburg_layout_toolbar(petersburg_widget_t *toolbar)
{
    petersburg_widget_t *child;
    int x = toolbar->x + 4;

    for (child = petersburg_widgets; child != NULL; child = child->next)
    {
        int width;
        if (child->parent_id != toolbar->id)
            continue;
        width = (int)strlen(child->text) * 7 + 20;
        child->x = x;
        child->y = toolbar->y + 3;
        child->width = width;
        child->height = toolbar->height - 6;
        child->laid_out = 1;
        x += width + 6;
    }
}

static void petersburg_render_button(const petersburg_widget_t *widget)
{
    int inset = 10;

    /* A checked (pressed) button shows the active tool or mode. */
    if (widget->checked)
        glColor3f(0.86f, 0.55f, 0.16f);
    else
        glColor3f(0.20f, 0.42f, 0.72f);
    petersburg_rect(widget->x, widget->y, widget->width, widget->height);
    petersburg_text(widget->x + inset,
                    widget->y + (widget->height - 7) / 2,
                    widget->text, 1.0f, 1.0f, 1.0f,
                    widget->width - inset * 2);
}

/* Draws the retained primitives clipped to the canvas rectangle. */
static void petersburg_render_canvas(
    const petersburg_widget_t *canvas,
    int framebuffer_height
)
{
    const double pi = 3.14159265358979323846;
    double ox = canvas->x;
    double oy = canvas->y;
    size_t index;

    if (canvas->width <= 0 || canvas->height <= 0)
        return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(canvas->x, framebuffer_height - canvas->y - canvas->height,
              canvas->width, canvas->height);
    petersburg_color(canvas->background);
    petersburg_rect((float)canvas->x, (float)canvas->y,
                    (float)canvas->width, (float)canvas->height);
    for (index = 0; index < canvas->shape_count; ++index)
    {
        const petersburg_shape_t *shape = &canvas->shapes[index];
        const double *v = shape->v;
        int i;
        int segments;
        double start;
        double sweep;

        petersburg_color(shape->color);
        switch (shape->kind)
        {
        case PETERSBURG_SHAPE_LINE:
            glBegin(GL_LINES);
            glVertex2d(ox + v[0] + 0.5, oy + v[1] + 0.5);
            glVertex2d(ox + v[2] + 0.5, oy + v[3] + 0.5);
            glEnd();
            break;
        case PETERSBURG_SHAPE_CIRCLE:
        case PETERSBURG_SHAPE_ARC:
            if (shape->kind == PETERSBURG_SHAPE_CIRCLE)
            {
                start = 0.0;
                sweep = 2.0 * pi;
            }
            else
            {
                start = v[3] * pi / 180.0;
                sweep = (v[4] - v[3]) * pi / 180.0;
                while (sweep <= 0.0)
                    sweep += 2.0 * pi;
            }
            segments = petersburg_arc_segments(v[2], sweep);
            glBegin(shape->kind == PETERSBURG_SHAPE_CIRCLE
                        ? GL_LINE_LOOP : GL_LINE_STRIP);
            for (i = 0; i <= segments; ++i)
            {
                double angle = start + sweep * i / segments;
                if (shape->kind == PETERSBURG_SHAPE_CIRCLE && i == segments)
                    break;
                /* Angles run counter-clockwise as seen on screen. */
                glVertex2d(ox + v[0] + v[2] * cos(angle) + 0.5,
                           oy + v[1] - v[2] * sin(angle) + 0.5);
            }
            glEnd();
            break;
        case PETERSBURG_SHAPE_RECT:
            glBegin(GL_LINE_LOOP);
            glVertex2d(ox + v[0] + 0.5, oy + v[1] + 0.5);
            glVertex2d(ox + v[0] + v[2] - 0.5, oy + v[1] + 0.5);
            glVertex2d(ox + v[0] + v[2] - 0.5, oy + v[1] + v[3] - 0.5);
            glVertex2d(ox + v[0] + 0.5, oy + v[1] + v[3] - 0.5);
            glEnd();
            break;
        case PETERSBURG_SHAPE_FILL:
            petersburg_rect((float)(ox + v[0]), (float)(oy + v[1]),
                            (float)v[2], (float)v[3]);
            break;
        case PETERSBURG_SHAPE_TEXT:
            /* v[2] is the scale of canvastextscale, 0 for canvastext */
            petersburg_text_scaled((float)(ox + v[0]), (float)(oy + v[1]),
                                   shape->text,
                                   (float)((shape->color >> 16) & 255) / 255.0f,
                                   (float)((shape->color >> 8) & 255) / 255.0f,
                                   (float)(shape->color & 255) / 255.0f,
                                   canvas->width,
                                   v[2] >= 1.0 ? (int)v[2] : 1);
            break;
        case PETERSBURG_SHAPE_FONT_TEXT:
            petersburg_font_text((float)(ox + v[0]), (float)(oy + v[1]),
                                 shape->text, shape->color, (long)v[3], v[2],
                                 (float)(ox + canvas->width));
            break;
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

static void petersburg_render_window(petersburg_widget_t *window)
{
    GLFWwindow *native = window->native_window;
    petersburg_widget_t *widget;
    int width;
    int height;

    if (native == NULL || glfwWindowShouldClose(native))
        return;
    glfwMakeContextCurrent(native);
    glfwGetFramebufferSize(native, &width, &height);
    if (width <= 0 || height <= 0)
        return;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    window->width = width;
    window->height = height;
    petersburg_layout(window);

    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        int inset = widget->type == PETERSBURG_LABEL ? 4 : 10;
        if (widget->parent_id != window->id ||
            widget->type == PETERSBURG_WINDOW)
            continue;
        widget->laid_out = 1;

        switch (widget->type)
        {
        case PETERSBURG_LABEL:
            petersburg_text(widget->x, widget->y + 8, widget->text,
                            0.92f, 0.94f, 0.98f, widget->width);
            break;
        case PETERSBURG_BUTTON:
            petersburg_render_button(widget);
            break;
        case PETERSBURG_TOOLBAR:
            {
                petersburg_widget_t *child;
                glColor3f(0.17f, 0.20f, 0.26f);
                petersburg_rect(widget->x, widget->y, widget->width,
                                widget->height);
                petersburg_layout_toolbar(widget);
                for (child = petersburg_widgets; child != NULL;
                     child = child->next)
                    if (child->parent_id == widget->id)
                        petersburg_render_button(child);
            }
            break;
        case PETERSBURG_TEXTBOX:
            glColor3f(0.07f, 0.08f, 0.11f);
            petersburg_rect(widget->x, widget->y, widget->width,
                            widget->height);
            petersburg_text(widget->x + inset, widget->y + 12,
                            widget->text, 0.95f, 0.96f, 0.98f,
                            widget->width - inset * 2);
            break;
        case PETERSBURG_CHECKBOX:
            glColor3f(0.86f, 0.89f, 0.94f);
            petersburg_rect(widget->x, widget->y + 9, 18, 18);
            if (widget->checked)
            {
                glColor3f(0.18f, 0.55f, 0.88f);
                petersburg_rect(widget->x + 4, widget->y + 13, 10, 10);
            }
            petersburg_text(widget->x + 28, widget->y + 14,
                            widget->text, 0.92f, 0.94f, 0.98f,
                            widget->width - 32);
            break;
        case PETERSBURG_LIST:
            glColor3f(0.07f, 0.08f, 0.11f);
            petersburg_rect(widget->x, widget->y, widget->width,
                            widget->height);
            if (widget->text != NULL)
            {
                int row = 0;
                int visible = widget->height / 18;
                if (visible < 1)
                    visible = 1;
                if (widget->selected < widget->scroll)
                    widget->scroll = widget->selected;
                if (widget->selected >= widget->scroll + visible)
                    widget->scroll = widget->selected - visible + 1;
                for (row = 0; row < visible; ++row)
                {
                    size_t item_size = strlen(widget->text) + 1;
                    char *item = malloc(item_size);
                    if (item == NULL)
                    {
                        fprintf(stderr,
                                "USSR: Petersburg could not render list items\n");
                        petersburg_last_error = 1;
                        break;
                    }
                    if (!petersburg_item_at(widget, widget->scroll + row,
                                            item, item_size))
                    {
                        free(item);
                        break;
                    }
                    {
                        size_t item_index = (size_t)(widget->scroll + row);
                        int marked = item_index < widget->mark_count &&
                                     widget->marks[item_index];
                        if (marked)
                        {
                            /* Marked rows: amber band and text, kept
                             * distinct from the blue cursor band. */
                            glColor3f(0.36f, 0.27f, 0.08f);
                            petersburg_rect(widget->x + 2,
                                            widget->y + 2 + row * 18,
                                            widget->width - 4, 17);
                        }
                        if (widget->scroll + row == widget->selected)
                        {
                            if (petersburg_focus == widget)
                                glColor3f(0.18f, 0.37f, 0.62f);
                            else
                                glColor3f(0.12f, 0.22f, 0.34f);
                            petersburg_rect(widget->x + (marked ? 8 : 2),
                                            widget->y + 2 + row * 18,
                                            widget->width - (marked ? 10 : 4),
                                            17);
                        }
                        if (marked)
                        {
                            glColor3f(1.0f, 0.74f, 0.20f);
                            petersburg_rect(widget->x + 2,
                                            widget->y + 2 + row * 18, 4, 17);
                        }
                        petersburg_text(widget->x + 12,
                                        widget->y + 7 + row * 18,
                                        item,
                                        marked ? 1.0f : 0.92f,
                                        marked ? 0.80f : 0.94f,
                                        marked ? 0.30f : 0.98f,
                                        widget->width - 20);
                    }
                    free(item);
                }
            }
            break;
        case PETERSBURG_COMBOBOX:
            glColor3f(0.07f, 0.08f, 0.11f);
            petersburg_rect(widget->x, widget->y, widget->width,
                            widget->height);
            if (widget->text != NULL)
            {
                char item[256];
                petersburg_text(widget->x + 10, widget->y + 13,
                                petersburg_selected_item(widget, item,
                                                         sizeof(item)),
                                0.92f, 0.94f, 0.98f,
                                widget->width - 20);
            }
            petersburg_text(widget->x + widget->width - 16,
                            widget->y + 13, "v", 0.92f, 0.94f, 0.98f, 8);
            break;
        case PETERSBURG_IMAGE:
            if (widget->pixels != NULL && widget->image_width > 0 &&
                widget->image_height > 0)
            {
                glRasterPos2i(widget->x, widget->y + widget->height);
                glPixelZoom(
                    (float)widget->width / widget->image_width,
                    -(float)widget->height / widget->image_height
                );
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glDrawPixels(widget->image_width, widget->image_height,
                             GL_RGB, GL_UNSIGNED_BYTE, widget->pixels);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelZoom(1.0f, 1.0f);
            }
            break;
        case PETERSBURG_CANVAS:
            petersburg_render_canvas(widget, height);
            break;
        default:
            break;
        }
    }
    glfwSwapBuffers(native);
}

static petersburg_widget_t *petersburg_window_for(GLFWwindow *native)
{
    return (petersburg_widget_t *)glfwGetWindowUserPointer(native);
}

static void petersburg_mouse_button(GLFWwindow *native, int button,
                                    int action, int modifiers)
{
    petersburg_widget_t *window = petersburg_window_for(native);
    petersburg_widget_t *widget;
    double mouse_x;
    double mouse_y;
    (void)modifiers;

    if (window == NULL || action != GLFW_PRESS ||
        (button != GLFW_MOUSE_BUTTON_LEFT &&
         button != GLFW_MOUSE_BUTTON_RIGHT))
        return;
    glfwGetCursorPos(native, &mouse_x, &mouse_y);
    {
        int logical_width;
        int logical_height;
        int framebuffer_width;
        int framebuffer_height;
        glfwGetWindowSize(native, &logical_width, &logical_height);
        glfwGetFramebufferSize(native, &framebuffer_width, &framebuffer_height);
        if (logical_width > 0 && logical_height > 0)
        {
            mouse_x *= (double)framebuffer_width / logical_width;
            mouse_y *= (double)framebuffer_height / logical_height;
        }
    }
    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        if (widget->parent_id != window->id ||
            mouse_x < widget->x || mouse_x >= widget->x + widget->width ||
            mouse_y < widget->y || mouse_y >= widget->y + widget->height)
            continue;
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            /* only canvases take the right button: their "menu" event */
            if (widget->type == PETERSBURG_CANVAS)
                petersburg_queue(widget, widget->menu_callback, 2,
                                 (long)(mouse_x - widget->x),
                                 (long)(mouse_y - widget->y), 0);
            return;
        }
        if (widget->type == PETERSBURG_TEXTBOX ||
            widget->type == PETERSBURG_LIST)
            petersburg_focus = widget;
        else
            petersburg_focus = NULL;
        if (widget->type == PETERSBURG_CHECKBOX)
        {
            widget->checked = !widget->checked;
            petersburg_queue_event(widget, 1);
        }
        else if (widget->type == PETERSBURG_TEXTBOX)
            petersburg_focus = widget;
        else if (widget->type == PETERSBURG_LIST ||
                 widget->type == PETERSBURG_COMBOBOX)
        {
            int item_count = petersburg_item_count(widget->text);
            if (item_count > 0)
            {
                if (widget->type == PETERSBURG_LIST)
                {
                    int row = (int)(mouse_y - widget->y - 2) / 18;
                    if (row < 0)
                        row = 0;
                    if (widget->scroll + row >= item_count)
                    {
                        petersburg_queue_event(widget, 1);
                        return;
                    }
                    widget->selected = widget->scroll + row;
                    if (widget->selected < 0)
                        widget->selected = 0;
                    if (widget->selected >= item_count)
                        widget->selected = item_count - 1;
                }
                else
                    widget->selected =
                        (widget->selected + 1) % item_count;
                if (widget->selected >= item_count)
                    widget->selected = item_count - 1;
            }
            petersburg_queue_event(widget, 1);
        }
        else if (widget->type == PETERSBURG_TOOLBAR)
        {
            petersburg_widget_t *child;
            for (child = petersburg_widgets; child != NULL; child = child->next)
                if (child->parent_id == widget->id &&
                    mouse_x >= child->x && mouse_x < child->x + child->width &&
                    mouse_y >= child->y && mouse_y < child->y + child->height)
                {
                    petersburg_queue_event(child, 0);
                    break;
                }
        }
        else if (widget->type == PETERSBURG_CANVAS)
            petersburg_canvas_press(widget,
                                    (int)(mouse_x - widget->x),
                                    (int)(mouse_y - widget->y),
                                    glfwGetTime());
        else if (widget->type == PETERSBURG_IMAGE)
        {
            int local_x = (int)((mouse_x - widget->x) *
                                widget->image_width / widget->width);
            int local_y = (int)((mouse_y - widget->y) *
                                widget->image_height / widget->height);
            petersburg_queue_pointer(widget, local_x, local_y);
        }
        else
            petersburg_queue_event(widget, 0);
        return;
    }
    petersburg_focus = NULL;
}

static void petersburg_character(GLFWwindow *native, unsigned int codepoint)
{
    char encoded[5] = {0, 0, 0, 0, 0};
    size_t length;
    size_t old_length;
    char *text;
    petersburg_widget_t *window = petersburg_window_for(native);

    /* typed text for the window, any script (Cyrillic too) */
    if (window != NULL && codepoint >= 32 && codepoint != 127 &&
        codepoint <= 0x10FFFF)
        petersburg_queue(window, window->char_callback, 1, (long)codepoint,
                         0, 0);
    if (petersburg_focus == NULL || codepoint < 32 || codepoint > 126)
        return;
    encoded[0] = (char)codepoint;
    length = strlen(encoded);
    old_length = strlen(petersburg_focus->text);
    if (old_length > SIZE_MAX - length - 1)
        return;
    text = realloc(petersburg_focus->text, old_length + length + 1);
    if (text == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not grow text-box content\n");
        petersburg_last_error = 1;
        return;
    }
    memcpy(text + old_length, encoded, length + 1);
    petersburg_focus->text = text;
    petersburg_queue_event(petersburg_focus, 1);
}

static void petersburg_key(GLFWwindow *native, int key, int scancode,
                           int action, int modifiers)
{
    petersburg_widget_t *window = petersburg_window_for(native);
    size_t length;
    char *text;
    (void)scancode;
    (void)modifiers;

    if (action == GLFW_RELEASE)
    {
        if (window != NULL)
            petersburg_queue(window, window->keyup_callback, 1, key, 0, 0);
        return;
    }
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    if (action == GLFW_PRESS)
        petersburg_queue_key(window, key);
    if (petersburg_focus == NULL || action != GLFW_PRESS)
        return;
    if (petersburg_focus->type == PETERSBURG_LIST)
    {
        int count = petersburg_item_count(petersburg_focus->text);
        if (key == GLFW_KEY_UP && petersburg_focus->selected > 0)
            --petersburg_focus->selected;
        else if (key == GLFW_KEY_DOWN &&
                 petersburg_focus->selected + 1 < count)
            ++petersburg_focus->selected;
        else
            return;
        petersburg_queue_event(petersburg_focus, 1);
        return;
    }
    if (petersburg_focus->type != PETERSBURG_TEXTBOX ||
        key != GLFW_KEY_BACKSPACE)
        return;
    length = strlen(petersburg_focus->text);
    if (length == 0)
        return;
    text = realloc(petersburg_focus->text, length);
    if (text == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not shrink text-box content\n");
        petersburg_last_error = 1;
        return;
    }
    text[length - 1] = '\0';
    petersburg_focus->text = text;
    petersburg_queue_event(petersburg_focus, 1);
}

static void petersburg_window_focus(GLFWwindow *native, int focused)
{
    petersburg_widget_t *window = petersburg_window_for(native);

    if (window != NULL)
        petersburg_queue(window, window->focus_callback, 1,
                         focused ? 1 : 0, 0, 0);
}

/* Wheel input goes to the canvas under the cursor as (x, y, +1/-1). */
static void petersburg_scroll(GLFWwindow *native, double x_offset,
                              double y_offset)
{
    petersburg_widget_t *window = petersburg_window_for(native);
    petersburg_widget_t *widget;
    double mouse_x;
    double mouse_y;
    int logical_width;
    int logical_height;
    int framebuffer_width;
    int framebuffer_height;
    (void)x_offset;

    if (window == NULL || y_offset == 0.0)
        return;
    glfwGetCursorPos(native, &mouse_x, &mouse_y);
    glfwGetWindowSize(native, &logical_width, &logical_height);
    glfwGetFramebufferSize(native, &framebuffer_width, &framebuffer_height);
    if (logical_width > 0 && logical_height > 0)
    {
        mouse_x *= (double)framebuffer_width / logical_width;
        mouse_y *= (double)framebuffer_height / logical_height;
    }
    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        if (widget->parent_id != window->id ||
            widget->type != PETERSBURG_CANVAS ||
            mouse_x < widget->x || mouse_x >= widget->x + widget->width ||
            mouse_y < widget->y || mouse_y >= widget->y + widget->height)
            continue;
        petersburg_queue(widget, widget->wheel_callback, 3,
                         (long)(mouse_x - widget->x),
                         (long)(mouse_y - widget->y),
                         y_offset > 0.0 ? 1 : -1);
        return;
    }
}

static int petersburg_ppm_token(
    FILE *file,
    char *token,
    size_t token_capacity
)
{
    int character;
    size_t length = 0;

    for (;;)
    {
        character = fgetc(file);
        if (character == EOF)
            return 0;
        if (character == '#')
        {
            do
                character = fgetc(file);
            while (character != EOF && character != '\n');
            if (character == EOF)
                return 0;
        }
        else if (!isspace((unsigned char)character))
            break;
    }

    while (character != EOF && !isspace((unsigned char)character))
    {
        if (length + 1 >= token_capacity)
            return 0;
        token[length++] = (char)character;
        character = fgetc(file);
    }
    if (character == EOF || length == 0)
        return 0;
    if (character == '\r')
    {
        int next = fgetc(file);
        if (next != '\n' && next != EOF)
            ungetc(next, file);
    }
    token[length] = '\0';
    return 1;
}

static int petersburg_ppm_integer(FILE *file, int *out)
{
    char token[32];
    char *end;
    long value;

    if (!petersburg_ppm_token(file, token, sizeof(token)))
        return 0;
    errno = 0;
    value = strtol(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0' ||
        value <= 0 || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int petersburg_read_ppm(petersburg_widget_t *widget, const char *path)
{
    FILE *file = fopen(path, "rb");
    char magic[8];
    int max_value = 0;
    size_t pixels;

    if (file == NULL)
    {
        fprintf(stderr, "USSR: Petersburg cannot open image '%s'\n", path);
        return 0;
    }
    if (!petersburg_ppm_token(file, magic, sizeof(magic)) ||
        strcmp(magic, "P6") != 0 ||
        !petersburg_ppm_integer(file, &widget->image_width) ||
        !petersburg_ppm_integer(file, &widget->image_height) ||
        !petersburg_ppm_integer(file, &max_value) || max_value != 255 ||
        (size_t)widget->image_width >
            SIZE_MAX / (size_t)widget->image_height / 3)
    {
        fprintf(stderr, "USSR: Petersburg expects a valid P6 PPM image\n");
        fclose(file);
        return 0;
    }
    pixels = (size_t)widget->image_width * widget->image_height * 3;
    widget->pixels = malloc(pixels);
    if (widget->pixels == NULL || fread(widget->pixels, 1, pixels, file) != pixels)
    {
        free(widget->pixels);
        widget->pixels = NULL;
        fclose(file);
        fprintf(stderr, "USSR: Petersburg could not read image pixels\n");
        return 0;
    }
    fclose(file);
    return 1;
}

static void petersburg_canvas_reset(petersburg_widget_t *canvas)
{
    size_t index;

    for (index = 0; index < canvas->shape_count; ++index)
        free(canvas->shapes[index].text);
    canvas->shape_count = 0;
}

static void petersburg_free_widget(petersburg_widget_t *widget)
{
    if (widget->native_window != NULL)
    {
        petersburg_glyphs_forget(widget->native_window);
        glfwSetWindowUserPointer(widget->native_window, NULL);
        glfwDestroyWindow(widget->native_window);
    }
    petersburg_canvas_reset(widget);
    free(widget->shapes);
    free(widget->marks);
    free(widget->pixels);
    free(widget->text);
    free(widget->click_callback);
    free(widget->change_callback);
    free(widget->key_callback);
    free(widget->keyup_callback);
    free(widget->focus_callback);
    free(widget->wheel_callback);
    free(widget->menu_callback);
    free(widget->double_callback);
    free(widget->char_callback);
    free(widget);
}

static void petersburg_discard_events(long widget_id)
{
    petersburg_event_t pending[sizeof(petersburg_events) /
                               sizeof(petersburg_events[0])];
    size_t keep = 0;
    size_t i;

    for (i = 0; i < petersburg_event_count; ++i)
    {
        petersburg_event_t *event = &petersburg_events[
            (petersburg_event_head + i) %
            (sizeof(petersburg_events) / sizeof(petersburg_events[0]))
        ];
        if (event->widget_id != widget_id)
            pending[keep++] = *event;
    }
    memcpy(petersburg_events, pending, keep * sizeof(*pending));
    petersburg_event_head = 0;
    petersburg_event_count = keep;
}

static int petersburg_destroy(long id)
{
    petersburg_widget_t **link = &petersburg_widgets;
    petersburg_widget_t *target = petersburg_find(id);
    int is_window;
    long target_id;

    if (target == NULL)
        return 0;
    /* Containers first drop their toolbars, whose buttons are grandchildren
     * of the window. */
    if (target->type == PETERSBURG_WINDOW)
    {
        petersburg_widget_t *child = petersburg_widgets;
        while (child != NULL)
        {
            if (child->parent_id == target->id &&
                child->type == PETERSBURG_TOOLBAR)
            {
                (void)petersburg_destroy(child->id);
                child = petersburg_widgets;
                continue;
            }
            child = child->next;
        }
    }
    is_window = target->type == PETERSBURG_WINDOW ||
                target->type == PETERSBURG_TOOLBAR;
    /* target is freed inside the loop while its children are still being
     * looked for: compare against a copy of its id. */
    target_id = target->id;
    while (*link != NULL)
    {
        petersburg_widget_t *widget = *link;
        if (widget->id == target_id ||
            (is_window && widget->parent_id == target_id))
        {
            *link = widget->next;
            petersburg_discard_events(widget->id);
            if (petersburg_focus == widget)
                petersburg_focus = NULL;
            petersburg_free_widget(widget);
            if (!is_window)
                break;
        }
        else
            link = &widget->next;
    }
    return 1;
}

static int petersburg_create_widget(
    const char *name,
    petersburg_type_t type,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    long parent_id;
    const char *text;
    petersburg_widget_t *parent;
    petersburg_widget_t *widget;
    const size_t expected = 2;

    if (!petersburg_arity(name, value_count, expected) ||
        !petersburg_expect_integer(values, 0, &parent_id) ||
        !petersburg_expect_string(values, 1, &text))
    {
        fprintf(stderr, "USSR: %s expects a window handle and string\n", name);
        return -1;
    }
    parent = petersburg_find(parent_id);
    if (parent == NULL ||
        (parent->type != PETERSBURG_WINDOW &&
         !(parent->type == PETERSBURG_TOOLBAR &&
           type == PETERSBURG_BUTTON)))
    {
        fprintf(stderr, "USSR: %s received an invalid window handle\n", name);
        return -1;
    }
    widget = petersburg_add(type, parent_id, text);
    if (widget == NULL)
        return -1;
    if (type == PETERSBURG_IMAGE && !petersburg_read_ppm(widget, text))
    {
        petersburg_destroy(widget->id);
        return -1;
    }
    out->type = USSR_INTEGER;
    out->data.integer = widget->id;
    return 1;
}

static int petersburg_create_window(
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    const char *title;
    long width;
    long height;
    petersburg_widget_t *widget;
    GLFWwindow *native;

    if (!petersburg_arity("petersburg_create_window", value_count, 3) ||
        !petersburg_expect_string(values, 0, &title) ||
        !petersburg_expect_integer(values, 1, &width) ||
        !petersburg_expect_integer(values, 2, &height) ||
        width < 160 || width > 8192 || height < 120 || height > 8192)
    {
        fprintf(stderr,
                "USSR: petersburg_create_window expects title, width, height\n");
        return -1;
    }
    if (!petersburg_initialize())
        return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    native = glfwCreateWindow((int)width, (int)height, title, NULL, NULL);
    if (native == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not create an OpenGL window\n");
        return -1;
    }
    widget = petersburg_add(PETERSBURG_WINDOW, 0, title);
    if (widget == NULL)
    {
        glfwDestroyWindow(native);
        return -1;
    }
    widget->width = (int)width;
    widget->height = (int)height;
    widget->native_window = native;
    glfwSetWindowUserPointer(native, widget);
    glfwSetMouseButtonCallback(native, petersburg_mouse_button);
    glfwSetCharCallback(native, petersburg_character);
    glfwSetKeyCallback(native, petersburg_key);
    glfwSetWindowFocusCallback(native, petersburg_window_focus);
    glfwSetScrollCallback(native, petersburg_scroll);
    glfwMakeContextCurrent(native);
    glfwSwapInterval(1);
    out->type = USSR_INTEGER;
    out->data.integer = widget->id;
    return 1;
}

static int petersburg_reload_image(
    petersburg_widget_t *widget,
    const char *path
)
{
    petersburg_widget_t image = {0};

    if (!petersburg_read_ppm(&image, path))
        return 0;
    free(widget->pixels);
    widget->pixels = image.pixels;
    widget->image_width = image.image_width;
    widget->image_height = image.image_height;
    return 1;
}

static int petersburg_set_callback(
    const ussr_value_t *values,
    size_t value_count
)
{
    long id;
    const char *event_name;
    const char *callback;
    petersburg_widget_t *widget;
    char **slot;
    char *copy;

    if (!petersburg_arity("petersburg_on", value_count, 3) ||
        !petersburg_expect_integer(values, 0, &id) ||
        !petersburg_expect_string(values, 1, &event_name) ||
        !petersburg_expect_string(values, 2, &callback))
    {
        fprintf(stderr,
                "USSR: petersburg_on expects handle, event, callback name\n");
        return -1;
    }
    widget = petersburg_find(id);
    if (widget == NULL)
    {
        fprintf(stderr, "USSR: petersburg_on received an invalid widget\n");
        return -1;
    }
    if (strcmp(event_name, "click") == 0)
        slot = &widget->click_callback;
    else if (strcmp(event_name, "change") == 0)
        slot = &widget->change_callback;
    else if (strcmp(event_name, "pointer") == 0 &&
             widget->type == PETERSBURG_IMAGE)
        slot = &widget->click_callback;
    else if (strcmp(event_name, "pointer") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        slot = &widget->click_callback;
    else if (strcmp(event_name, "wheel") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        slot = &widget->wheel_callback;
    else if (strcmp(event_name, "menu") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        slot = &widget->menu_callback;
    else if (strcmp(event_name, "double") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        slot = &widget->double_callback;
    else if (strcmp(event_name, "key") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        slot = &widget->key_callback;
    else if (strcmp(event_name, "keyup") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        slot = &widget->keyup_callback;
    else if (strcmp(event_name, "focus") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        slot = &widget->focus_callback;
    else if (strcmp(event_name, "char") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        slot = &widget->char_callback;
    else
    {
        fprintf(stderr,
                "USSR: Petersburg event must be click/change on a widget, pointer on an image or canvas, wheel/menu/double on a canvas, or key/keyup/focus/char on a window\n");
        return -1;
    }
    copy = petersburg_strdup(callback);
    if (copy == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not copy callback name\n");
        return -1;
    }
    if (strlen(copy) >= sizeof(petersburg_events[0].callback))
    {
        free(copy);
        fprintf(stderr, "USSR: Petersburg callback name is too long\n");
        return -1;
    }
    free(*slot);
    *slot = copy;
    return 1;
}

static int petersburg_message_box(
    const ussr_value_t *values,
    size_t value_count
)
{
    const char *title;
    const char *message;
    GLFWwindow *window;

    if (!petersburg_arity("petersburg_message_box", value_count, 2) ||
        !petersburg_expect_string(values, 0, &title) ||
        !petersburg_expect_string(values, 1, &message))
    {
        fprintf(stderr, "USSR: petersburg_message_box expects title and text\n");
        return -1;
    }
    if (!petersburg_initialize())
        return -1;
    window = glfwCreateWindow(380, 150, title, NULL, NULL);
    if (window == NULL)
    {
        fprintf(stderr, "USSR: Petersburg could not create a message box\n");
        return -1;
    }
    glfwMakeContextCurrent(window);
    while (!glfwWindowShouldClose(window))
    {
        int width;
        int height;
        int key;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        petersburg_text(20, 30, message, 0.95f, 0.96f, 0.98f, width - 40);
        glColor3f(0.20f, 0.42f, 0.72f);
        petersburg_rect(width - 110, height - 48, 90, 30);
        petersburg_text(width - 82, height - 37, "OK",
                        1.0f, 1.0f, 1.0f, 64);
        glfwSwapBuffers(window);
        glfwPollEvents();
        key = glfwGetKey(window, GLFW_KEY_ENTER);
        if (key == GLFW_PRESS || glfwGetMouseButton(
                window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    glfwDestroyWindow(window);
    return 1;
}

static int petersburg_find_function(
    const ussr_sched_program_t *program,
    const char *name
)
{
    size_t i;
    if (program == NULL)
        return -1;
    for (i = 0; i < program->bc.function_count; ++i)
        if (strcmp(program->bc.functions[i].name, name) == 0)
            return (int)i;
    return -1;
}

static int petersburg_color_value(
    const ussr_value_t *values,
    size_t index,
    unsigned long *color
)
{
    long raw;

    if (!petersburg_expect_integer(values, index, &raw) ||
        raw < 0 || raw > 0xFFFFFF)
        return 0;
    *color = (unsigned long)raw;
    return 1;
}

/* petersburg_canvas_<shape>(canvas, numbers..., [text,] color) */
static int petersburg_canvas_shape(
    const char *name,
    petersburg_shape_kind_t kind,
    size_t number_count,
    int has_text,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    petersburg_widget_t *canvas;
    petersburg_shape_t shape;
    const char *text = NULL;
    long id;
    size_t index;

    memset(&shape, 0, sizeof(shape));
    shape.kind = kind;
    if (!petersburg_arity(name, value_count,
                          2 + number_count + (has_text ? 1 : 0)) ||
        !petersburg_expect_integer(values, 0, &id) ||
        (canvas = petersburg_find(id)) == NULL ||
        canvas->type != PETERSBURG_CANVAS)
    {
        fprintf(stderr, "USSR: %s expects a canvas handle\n", name);
        return -1;
    }
    for (index = 0; index < number_count; ++index)
        if (!petersburg_expect_number(values, 1 + index, &shape.v[index]))
        {
            fprintf(stderr, "USSR: %s expects finite numeric coordinates\n",
                    name);
            return -1;
        }
    if (has_text &&
        !petersburg_expect_string(values, 1 + number_count, &text))
    {
        fprintf(stderr, "USSR: %s expects a text string\n", name);
        return -1;
    }
    if (!petersburg_color_value(values, value_count - 1, &shape.color))
    {
        fprintf(stderr, "USSR: %s expects a 0xRRGGBB color integer\n", name);
        return -1;
    }
    if ((kind == PETERSBURG_SHAPE_CIRCLE || kind == PETERSBURG_SHAPE_ARC) &&
        shape.v[2] < 0.0)
    {
        fprintf(stderr, "USSR: %s expects a non-negative radius\n", name);
        return -1;
    }
    if (canvas->shape_count >= PETERSBURG_MAX_SHAPES)
    {
        fprintf(stderr, "USSR: Petersburg canvas holds at most %d shapes\n",
                PETERSBURG_MAX_SHAPES);
        return -1;
    }
    if (canvas->shape_count == canvas->shape_capacity)
    {
        size_t capacity = canvas->shape_capacity == 0
                              ? 64 : canvas->shape_capacity * 2;
        petersburg_shape_t *grown = realloc(canvas->shapes,
                                            capacity * sizeof(*grown));
        if (grown == NULL)
        {
            fprintf(stderr, "USSR: Petersburg could not grow a canvas\n");
            return -1;
        }
        canvas->shapes = grown;
        canvas->shape_capacity = capacity;
    }
    if (text != NULL)
    {
        shape.text = petersburg_strdup(text);
        if (shape.text == NULL)
            return -1;
    }
    canvas->shapes[canvas->shape_count++] = shape;
    *out = ussr_integer(0);
    return 1;
}

static int petersburg_list_mark(
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    int setting = strcmp(name, "petersburg_set_mark") == 0;
    petersburg_widget_t *widget;
    long id;
    long index;
    int count;

    if (!petersburg_arity(name, value_count, setting ? 3 : 2) ||
        !petersburg_expect_integer(values, 0, &id) ||
        !petersburg_expect_integer(values, 1, &index) ||
        (widget = petersburg_find(id)) == NULL ||
        widget->type != PETERSBURG_LIST ||
        (setting && values[2].type != USSR_BOOLEAN))
    {
        fprintf(stderr, "USSR: %s expects a list, row index%s\n", name,
                setting ? ", and boolean" : "");
        return -1;
    }
    count = petersburg_item_count(widget->text);
    if (index < 0 || index >= count)
    {
        fprintf(stderr, "USSR: %s row index is out of range\n", name);
        return -1;
    }
    if (!setting)
    {
        *out = ussr_boolean((size_t)index < widget->mark_count &&
                            widget->marks[index]);
        return 1;
    }
    if (widget->mark_count < (size_t)count)
    {
        unsigned char *grown = realloc(widget->marks, (size_t)count);
        if (grown == NULL)
        {
            fprintf(stderr, "USSR: Petersburg could not grow list marks\n");
            return -1;
        }
        memset(grown + widget->mark_count, 0,
               (size_t)count - widget->mark_count);
        widget->marks = grown;
        widget->mark_count = (size_t)count;
    }
    widget->marks[index] = values[2].data.boolean ? 1 : 0;
    *out = ussr_integer(0);
    return 1;
}

/*
 * petersburg_inject(widget, event, a, b, c): feeds a synthetic event through
 * the same paths as real GLFW input, so GUI scripts can be tested without a
 * person at the keyboard. Unused values are ignored.
 */
static int petersburg_inject(
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    petersburg_widget_t *widget;
    const char *event_name;
    long id;
    long a;
    long b;
    long c;

    if (!petersburg_arity("petersburg_inject", value_count, 5) ||
        !petersburg_expect_integer(values, 0, &id) ||
        !petersburg_expect_string(values, 1, &event_name) ||
        !petersburg_expect_integer(values, 2, &a) ||
        !petersburg_expect_integer(values, 3, &b) ||
        !petersburg_expect_integer(values, 4, &c) ||
        (widget = petersburg_find(id)) == NULL)
    {
        fprintf(stderr,
                "USSR: petersburg_inject expects widget, event, and three integers\n");
        return -1;
    }
    if ((strcmp(event_name, "key") == 0 ||
         strcmp(event_name, "keyup") == 0) &&
        widget->type == PETERSBURG_WINDOW)
        petersburg_key(widget->native_window, (int)a, 0,
                       event_name[3] == 'u' ? GLFW_RELEASE : GLFW_PRESS, 0);
    else if (strcmp(event_name, "focus") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        petersburg_window_focus(widget->native_window, a != 0);
    else if (strcmp(event_name, "char") == 0 &&
             widget->type == PETERSBURG_WINDOW)
        petersburg_character(widget->native_window, (unsigned int)a);
    else if (strcmp(event_name, "pointer") == 0 &&
             (widget->type == PETERSBURG_CANVAS ||
              widget->type == PETERSBURG_IMAGE))
        petersburg_queue_pointer(widget, (int)a, (int)b);
    else if (strcmp(event_name, "wheel") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        petersburg_queue(widget, widget->wheel_callback, 3, a, b,
                         c > 0 ? 1 : -1);
    else if (strcmp(event_name, "menu") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        petersburg_queue(widget, widget->menu_callback, 2, a, b, 0);
    else if (strcmp(event_name, "double") == 0 &&
             widget->type == PETERSBURG_CANVAS)
        petersburg_queue(widget, widget->double_callback, 2, a, b, 0);
    else if (strcmp(event_name, "click") == 0 &&
             widget->type != PETERSBURG_WINDOW)
        petersburg_queue_event(widget, 0);
    else
    {
        fprintf(stderr,
                "USSR: petersburg_inject cannot send '%s' to this widget\n",
                event_name);
        return -1;
    }
    *out = ussr_integer(0);
    return petersburg_last_error ? -1 : 1;
}

#endif

#ifdef USSR_WITH_GLFW
/*
 * petersburg_take_events(v): for a program that runs USSR code of its
 * own (russia.su): from the first call on, queued events are no longer
 * spawned as callbacks of the running program but kept for this call,
 * which hands them over and empties the queue. v is a vector: 1 while a
 * Petersburg run is going (petersburg_run or show, and a window left)
 * else 0, then one vector per event, oldest first: the callback name,
 * the widget handle and the event's integer values.
 */
static int petersburg_events_delegated;

static int petersburg_take_events(size_t value_count, ussr_value_t *out)
{
    ussr_vector_t *all;
    size_t i;
    size_t j;

    if (!petersburg_arity("petersburg_take_events", value_count, 0))
        return -1;
    petersburg_events_delegated = 1;
    all = ussr_vector_create(NULL);
    if (all == NULL ||
        ussr_vector_push(all, ussr_integer(ussr_petersburg_running())) != 0)
    {
        if (all != NULL)
            ussr_vector_release(all);
        return -1;
    }
    for (i = 0; i < petersburg_event_count; ++i)
    {
        const petersburg_event_t *event = &petersburg_events[
            (petersburg_event_head + i) %
            (sizeof(petersburg_events) / sizeof(petersburg_events[0]))
        ];
        ussr_vector_t *one = ussr_vector_create(NULL);
        ussr_value_t name_value;
        int failed;

        if (one == NULL)
        {
            ussr_vector_release(all);
            return -1;
        }
        name_value = ussr_string(event->callback);
        failed = name_value.data.string == NULL ||
                 ussr_vector_push(one, name_value) != 0 ||
                 ussr_vector_push(one, ussr_integer(event->widget_id)) != 0;
        ussr_value_free(&name_value);
        for (j = 0; !failed && j < event->value_count; ++j)
            failed = ussr_vector_push(one, ussr_integer(event->values[j])) != 0;
        if (!failed)
            failed = ussr_vector_push(all, ussr_vector_value(one)) != 0;
        ussr_vector_release(one);
        if (failed)
        {
            ussr_vector_release(all);
            return -1;
        }
    }
    petersburg_event_head =
        (petersburg_event_head + petersburg_event_count) %
        (sizeof(petersburg_events) / sizeof(petersburg_events[0]));
    petersburg_event_count = 0;
    *out = ussr_vector_value(all);
    return 1;
}
#endif

int ussr_petersburg_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    (void)program;
#ifndef USSR_WITH_GLFW
    (void)values;
    (void)value_count;
    (void)out;
    if (name != NULL && strncmp(name, "petersburg_", 11) == 0)
    {
        fprintf(stderr,
                "USSR: Petersburg requires GLFW; rebuild with GLFW support\n");
        return -1;
    }
    return 0;
#else
    long id;
    const char *text;
    petersburg_widget_t *widget;
    int status;

    if (name == NULL || strncmp(name, "petersburg_", 11) != 0)
        return 0;
    if (strcmp(name, "petersburg_create_window") == 0)
        return petersburg_create_window(values, value_count, out);
    if (strcmp(name, "petersburg_maximize") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_WINDOW)
        {
            fprintf(stderr, "USSR: petersburg_maximize expects a window handle\n");
            return -1;
        }
        glfwMaximizeWindow(widget->native_window);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_fullscreen") == 0)
    {
        /* A borderless window over the whole primary monitor; it stays an
         * ordinary window, so programs started from it open on top. 1 when
         * there is no monitor to cover and the window was maximized. */
        GLFWmonitor *monitor;
        const GLFWvidmode *mode;
        int mx = 0;
        int my = 0;
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_WINDOW)
        {
            fprintf(stderr, "USSR: petersburg_fullscreen expects a window handle\n");
            return -1;
        }
        monitor = glfwGetPrimaryMonitor();
        mode = monitor != NULL ? glfwGetVideoMode(monitor) : NULL;
        if (mode == NULL)
        {
            glfwMaximizeWindow(widget->native_window);
            *out = ussr_integer(1);
            return 1;
        }
        glfwGetMonitorPos(monitor, &mx, &my);
        widget->edge_to_edge = 1;
        /* started minimized (a shortcut set so): the desktop must show */
        glfwRestoreWindow(widget->native_window);
        glfwSetWindowAttrib(widget->native_window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(widget->native_window, mx, my);
        /* One row taller than the monitor: Windows still treats it as full
         * screen (the taskbar hides), but an exact match is shown by
         * independent flip, which screenshots and recorders read stale. */
        glfwSetWindowSize(widget->native_window, mode->width, mode->height + 1);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_create_label") == 0)
        return petersburg_create_widget(name, PETERSBURG_LABEL,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_button") == 0)
        return petersburg_create_widget(name, PETERSBURG_BUTTON,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_textbox") == 0)
        return petersburg_create_widget(name, PETERSBURG_TEXTBOX,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_checkbox") == 0)
        return petersburg_create_widget(name, PETERSBURG_CHECKBOX,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_list") == 0)
        return petersburg_create_widget(name, PETERSBURG_LIST,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_combobox") == 0)
        return petersburg_create_widget(name, PETERSBURG_COMBOBOX,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_toolbar") == 0)
        return petersburg_create_widget(name, PETERSBURG_TOOLBAR,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_create_image") == 0)
        return petersburg_create_widget(name, PETERSBURG_IMAGE,
                                        values, value_count, out);
    if (strcmp(name, "petersburg_on") == 0)
    {
        status = petersburg_set_callback(values, value_count);
        if (status > 0)
            *out = ussr_integer(0);
        return status > 0 ? 1 : -1;
    }
    if (strcmp(name, "petersburg_pack") == 0)
    {
        if (value_count != 2 ||
            !petersburg_expect_integer(values, 0, &id) ||
            (values[1].type != USSR_INTEGER &&
             values[1].type != USSR_BOOLEAN) ||
            (values[1].type == USSR_INTEGER &&
             values[1].data.integer != 0 &&
             values[1].data.integer != 1) ||
            (widget = petersburg_find(id)) == NULL)
        {
            fprintf(stderr, "USSR: petersburg_pack expects handle and expand flag\n");
            return -1;
        }
        widget->expand = values[1].type == USSR_BOOLEAN
                             ? values[1].data.boolean
                             : (int)values[1].data.integer;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_grid") == 0)
    {
        long row;
        long column;
        long expand;

        if (value_count != 4 ||
            !petersburg_expect_integer(values, 0, &id) ||
            !petersburg_expect_integer(values, 1, &row) ||
            !petersburg_expect_integer(values, 2, &column) ||
            (values[3].type != USSR_INTEGER &&
             values[3].type != USSR_BOOLEAN) ||
            row < 0 || row >= 256 || column < 0 || column > 1 ||
            (values[3].type == USSR_INTEGER &&
             values[3].data.integer != 0 &&
             values[3].data.integer != 1) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type == PETERSBURG_WINDOW)
        {
            fprintf(stderr,
                    "USSR: petersburg_grid expects widget, row, column, expand\n");
            return -1;
        }
        expand = values[3].type == USSR_BOOLEAN
                     ? values[3].data.boolean
                     : (long)values[3].data.integer;
        widget->grid_row = (int)row;
        widget->grid_column = (int)column;
        widget->in_grid = 1;
        widget->expand = (int)expand;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_get_index") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_LIST &&
             widget->type != PETERSBURG_COMBOBOX))
        {
            fprintf(stderr,
                    "USSR: petersburg_get_index expects a list or combo box\n");
            return -1;
        }
        *out = ussr_integer(widget->selected);
        return 1;
    }
    if (strcmp(name, "petersburg_set_index") == 0)
    {
        long index;
        int item_count;

        if (value_count != 2 ||
            !petersburg_expect_integer(values, 0, &id) ||
            !petersburg_expect_integer(values, 1, &index) ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_LIST &&
             widget->type != PETERSBURG_COMBOBOX))
        {
            fprintf(stderr,
                    "USSR: petersburg_set_index expects widget and index\n");
            return -1;
        }
        item_count = petersburg_item_count(widget->text);
        if (index < 0 || index >= item_count)
        {
            fprintf(stderr, "USSR: Petersburg selection index is out of range\n");
            return -1;
        }
        widget->selected = (int)index;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_set_focus") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_TEXTBOX &&
             widget->type != PETERSBURG_LIST))
        {
            fprintf(stderr,
                    "USSR: petersburg_set_focus expects a text box or list\n");
            return -1;
        }
        petersburg_focus = widget;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_set_text") == 0)
    {
        char *copy;
        if (value_count != 2 ||
            !petersburg_expect_integer(values, 0, &id) ||
            !petersburg_expect_string(values, 1, &text) ||
            (widget = petersburg_find(id)) == NULL)
        {
            fprintf(stderr, "USSR: petersburg_set_text expects handle and text\n");
            return -1;
        }
        copy = petersburg_strdup(text);
        if (copy == NULL)
        {
            fprintf(stderr, "USSR: Petersburg could not copy widget text\n");
            return -1;
        }
        free(widget->text);
        widget->text = copy;
        if (widget->type == PETERSBURG_LIST ||
            widget->type == PETERSBURG_COMBOBOX)
        {
            widget->selected = 0;
            widget->scroll = 0;
            free(widget->marks);
            widget->marks = NULL;
            widget->mark_count = 0;
        }
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_reload_image") == 0)
    {
        if (value_count != 2 ||
            !petersburg_expect_integer(values, 0, &id) ||
            !petersburg_expect_string(values, 1, &text) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_IMAGE)
        {
            fprintf(stderr,
                    "USSR: petersburg_reload_image expects image handle and PPM path\n");
            return -1;
        }
        if (!petersburg_reload_image(widget, text))
            return -1;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_get_text") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL)
        {
            fprintf(stderr, "USSR: petersburg_get_text received an invalid handle\n");
            return -1;
        }
        *out = ussr_string(widget->text);
        if (out->data.string == NULL)
        {
            fprintf(stderr, "USSR: Petersburg could not copy widget text\n");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "petersburg_get_selection") == 0)
    {
        char *item;
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_LIST &&
             widget->type != PETERSBURG_COMBOBOX))
        {
            fprintf(stderr,
                    "USSR: petersburg_get_selection expects a list or combo box\n");
            return -1;
        }
        item = petersburg_selected_item_copy(widget);
        if (item == NULL)
            return -1;
        *out = ussr_string(item);
        free(item);
        if (out->data.string == NULL)
            return -1;
        return 1;
    }
    if (strcmp(name, "petersburg_get_checked") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_CHECKBOX &&
             widget->type != PETERSBURG_BUTTON))
        {
            fprintf(stderr, "USSR: petersburg_get_checked expects a checkbox or button\n");
            return -1;
        }
        *out = ussr_boolean(widget->checked);
        return 1;
    }
    if (strcmp(name, "petersburg_set_checked") == 0)
    {
        if (value_count != 2 ||
            !petersburg_expect_integer(values, 0, &id) ||
            values[1].type != USSR_BOOLEAN ||
            (widget = petersburg_find(id)) == NULL ||
            (widget->type != PETERSBURG_CHECKBOX &&
             widget->type != PETERSBURG_BUTTON))
        {
            fprintf(stderr, "USSR: petersburg_set_checked expects checkbox or button and boolean\n");
            return -1;
        }
        widget->checked = values[1].data.boolean;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_destroy") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL)
        {
            fprintf(stderr, "USSR: %s expects a valid widget handle\n", name);
            return -1;
        }
        (void)petersburg_destroy(id);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_show") == 0 ||
        strcmp(name, "petersburg_close") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_WINDOW)
        {
            fprintf(stderr, "USSR: %s expects a valid window handle\n", name);
            return -1;
        }
        if (strcmp(name, "petersburg_show") == 0)
        {
            glfwShowWindow(widget->native_window);
            petersburg_run_requested = 1;
        }
        else if (strcmp(name, "petersburg_close") == 0)
            glfwSetWindowShouldClose(widget->native_window, GLFW_TRUE);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_take_events") == 0)
        return petersburg_take_events(value_count, out);
    if (strcmp(name, "petersburg_run") == 0)
    {
        if (!petersburg_arity(name, value_count, 0))
            return -1;
        petersburg_run_requested = 1;
        for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
            if (widget->type == PETERSBURG_WINDOW)
                glfwShowWindow(widget->native_window);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_create_canvas") == 0)
    {
        petersburg_widget_t *parent;
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (parent = petersburg_find(id)) == NULL ||
            parent->type != PETERSBURG_WINDOW)
        {
            fprintf(stderr, "USSR: %s expects a window handle\n", name);
            return -1;
        }
        widget = petersburg_add(PETERSBURG_CANVAS, id, "");
        if (widget == NULL)
            return -1;
        widget->background = 0xFAFAFA;
        *out = ussr_integer(widget->id);
        return 1;
    }
    if (strcmp(name, "petersburg_canvas_clear") == 0)
    {
        unsigned long color;
        if (!petersburg_arity(name, value_count, 2) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_CANVAS ||
            !petersburg_color_value(values, 1, &color))
        {
            fprintf(stderr,
                    "USSR: %s expects a canvas and 0xRRGGBB background\n", name);
            return -1;
        }
        petersburg_canvas_reset(widget);
        widget->background = color;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "petersburg_canvas_line") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_LINE, 4, 0,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_circle") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_CIRCLE, 3, 0,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_arc") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_ARC, 5, 0,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_rect") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_RECT, 4, 0,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_fill") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_FILL, 4, 0,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_text") == 0)
        return petersburg_canvas_shape(name, PETERSBURG_SHAPE_TEXT, 2, 1,
                                       values, value_count, out);
    if (strcmp(name, "petersburg_canvas_text_scaled") == 0)
    {
        /* canvas x y text color scale: text whose font pixels are scale
         * pixels square; the scale is kept in the shape's v[2] */
        long scale;
        int status;
        if (!petersburg_arity(name, value_count, 6) ||
            !petersburg_expect_integer(values, 5, &scale) ||
            scale < 1 || scale > 16)
        {
            fprintf(stderr,
                    "USSR: %s expects canvas, x, y, text, color, scale 1..16\n",
                    name);
            return -1;
        }
        status = petersburg_canvas_shape(name, PETERSBURG_SHAPE_TEXT, 2, 1,
                                         values, 5, out);
        if (status > 0 && petersburg_expect_integer(values, 0, &id) &&
            (widget = petersburg_find(id)) != NULL &&
            widget->shape_count > 0)
            widget->shapes[widget->shape_count - 1].v[2] = (double)scale;
        return status;
    }
    if (strcmp(name, "petersburg_font_info") == 0)
    {
        /* path -> "Family|Style", or "" when it is not a usable font */
        char family[256], style[128], info[400];
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_string(values, 0, &text))
        {
            fprintf(stderr, "USSR: %s expects a font file path\n", name);
            return -1;
        }
        info[0] = '\0';
        if (ttf_file_info(text, family, sizeof(family), style, sizeof(style)))
            snprintf(info, sizeof(info), "%s|%s", family, style);
        *out = ussr_string(info);
        return 1;
    }
    if (strcmp(name, "petersburg_font_load") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_string(values, 0, &text))
        {
            fprintf(stderr, "USSR: %s expects a font file path\n", name);
            return -1;
        }
        *out = ussr_integer(petersburg_font_load(text));
        return 1;
    }
    if (strcmp(name, "petersburg_font_width") == 0 ||
        strcmp(name, "petersburg_font_height") == 0)
    {
        /* font size [text]: the advance of text, or the line height, in
         * pixels at size pixels per em (rounded as drawing rounds it) */
        int width = name[16] == 'w';
        long font_handle;
        double size;
        ttf_font_t *font;
        if (!petersburg_arity(name, value_count, width ? 3 : 2) ||
            !petersburg_expect_integer(values, 0, &font_handle) ||
            (font = petersburg_font(font_handle)) == NULL ||
            !petersburg_expect_number(values, 1, &size) ||
            size < 1 || size > 1000 ||
            (width && !petersburg_expect_string(values, 2, &text)))
        {
            fprintf(stderr, "USSR: %s expects a font handle, a size 1..1000%s\n",
                    name, width ? " and text" : "");
            return -1;
        }
        size = floor(size + 0.5);
        if (width)
            *out = ussr_real(petersburg_font_width(font, size, text));
        else
            *out = ussr_integer((long)ceil(
                (font->ascent - font->descent + font->line_gap) *
                petersburg_font_scale(font, size)));
        return 1;
    }
    if (strcmp(name, "petersburg_canvas_font_text") == 0)
    {
        /* canvas x y text color font size */
        long font_handle;
        double size;
        if (!petersburg_arity(name, value_count, 7) ||
            !petersburg_expect_integer(values, 5, &font_handle) ||
            petersburg_font(font_handle) == NULL ||
            !petersburg_expect_number(values, 6, &size) ||
            size < 1 || size > 1000)
        {
            fprintf(stderr,
                    "USSR: %s expects canvas, x, y, text, color, font, size 1..1000\n",
                    name);
            return -1;
        }
        status = petersburg_canvas_shape(name, PETERSBURG_SHAPE_FONT_TEXT, 2, 1,
                                         values, 5, out);
        if (status > 0 && petersburg_expect_integer(values, 0, &id) &&
            (widget = petersburg_find(id)) != NULL &&
            widget->shape_count > 0)
        {
            widget->shapes[widget->shape_count - 1].v[2] = size;
            widget->shapes[widget->shape_count - 1].v[3] = (double)font_handle;
        }
        return status;
    }
    if (strcmp(name, "petersburg_canvas_count") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_CANVAS)
        {
            fprintf(stderr, "USSR: %s expects a canvas handle\n", name);
            return -1;
        }
        *out = ussr_integer((long)widget->shape_count);
        return 1;
    }
    if (strcmp(name, "petersburg_get_width") == 0 ||
        strcmp(name, "petersburg_get_height") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL)
        {
            fprintf(stderr, "USSR: %s expects a widget handle\n", name);
            return -1;
        }
        /* Zero until the first render has laid the widget out. */
        if (!widget->laid_out && widget->type != PETERSBURG_WINDOW)
            *out = ussr_integer(0);
        else
            *out = ussr_integer(name[15] == 'w' ? widget->width
                                                : widget->height);
        return 1;
    }
    if (strcmp(name, "petersburg_set_mark") == 0 ||
        strcmp(name, "petersburg_get_mark") == 0)
        return petersburg_list_mark(name, values, value_count, out);
    if (strcmp(name, "petersburg_get_focus") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id) ||
            (widget = petersburg_find(id)) == NULL ||
            widget->type != PETERSBURG_WINDOW)
        {
            fprintf(stderr, "USSR: %s expects a window handle\n", name);
            return -1;
        }
        /* Handle of the window's focused text box or list, or 0. */
        *out = ussr_integer(petersburg_focus != NULL &&
                            petersburg_focus->parent_id == id
                                ? petersburg_focus->id : 0);
        return 1;
    }
    if (strcmp(name, "petersburg_alive") == 0)
    {
        if (!petersburg_arity(name, value_count, 1) ||
            !petersburg_expect_integer(values, 0, &id))
        {
            fprintf(stderr, "USSR: %s expects a widget handle\n", name);
            return -1;
        }
        widget = petersburg_find(id);
        *out = ussr_boolean(widget != NULL &&
                            (widget->type != PETERSBURG_WINDOW ||
                             !glfwWindowShouldClose(widget->native_window)));
        return 1;
    }
    if (strcmp(name, "petersburg_inject") == 0)
        return petersburg_inject(values, value_count, out);
    if (strcmp(name, "petersburg_message_box") == 0)
    {
        status = petersburg_message_box(values, value_count);
        if (status > 0)
            *out = ussr_integer(0);
        return status > 0 ? 1 : -1;
    }
    fprintf(stderr, "USSR: unknown Petersburg native command '%s'\n", name);
    return -1;
#endif
}

int ussr_petersburg_pump(
    ussr_sched_program_t *program,
    ussr_proc_t *parent,
    int wait_for_events
)
{
#ifndef USSR_WITH_GLFW
    (void)program;
    (void)parent;
    (void)wait_for_events;
    return 0;
#else
    petersburg_widget_t *widget;
    size_t i;

    if (!petersburg_initialized)
        return 0;
    if (wait_for_events && petersburg_run_requested)
    {
        /* Never sleep past the next scheduler timer. */
        long timer = ussr_eq_next_timeout_ms();
        if (timer == 0)
            glfwPollEvents();
        else
            glfwWaitEventsTimeout(timer > 0 && timer < 50
                                      ? (double)timer / 1000.0 : 0.05);
    }
    else
        glfwPollEvents();
    if (petersburg_last_error)
        return -1;

    for (;;)
    {
        long closing_id = 0;
        for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
            if (widget->type == PETERSBURG_WINDOW &&
                glfwWindowShouldClose(widget->native_window))
            {
                closing_id = widget->id;
                break;
            }
        if (closing_id == 0)
            break;
        (void)petersburg_destroy(closing_id);
    }

    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
    {
        if (widget->type == PETERSBURG_WINDOW && petersburg_run_requested)
        {
            petersburg_render_window(widget);
            glfwShowWindow(widget->native_window);
        }
    }
    if (petersburg_last_error)
        return -1;

    /* petersburg_take_events collects them instead */
    for (i = 0; !petersburg_events_delegated && i < petersburg_event_count; ++i)
    {
        petersburg_event_t *event = &petersburg_events[
            (petersburg_event_head + i) %
            (sizeof(petersburg_events) / sizeof(petersburg_events[0]))
        ];
        int function = petersburg_find_function(program, event->callback);
        ussr_value_t arguments[4];
        size_t argument_count;
        size_t value_index;
        if (function < 0)
        {
            fprintf(stderr,
                    "USSR: Petersburg callback '%s' is not a definition\n",
                    event->callback);
            petersburg_last_error = 1;
            return -1;
        }
        arguments[0] = ussr_integer(event->widget_id);
        argument_count = 1;
        for (value_index = 0; value_index < event->value_count; ++value_index)
            arguments[argument_count++] =
                ussr_integer(event->values[value_index]);
        if (ussr_sched_spawn_func(parent, program, (size_t)function,
                                  arguments, argument_count,
                                  event->callback, 1) < 0)
        {
            petersburg_last_error = 1;
            return -1;
        }
    }
    if (!petersburg_events_delegated)
    {
        petersburg_event_head =
            (petersburg_event_head + petersburg_event_count) %
            (sizeof(petersburg_events) / sizeof(petersburg_events[0]));
        petersburg_event_count = 0;
    }
    if (petersburg_run_requested)
    {
        for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
            if (widget->type == PETERSBURG_WINDOW)
                return 0;
        petersburg_run_requested = 0;
    }
    return 0;
#endif
}

int ussr_petersburg_running(void)
{
#ifdef USSR_WITH_GLFW
    petersburg_widget_t *widget;
    if (!petersburg_run_requested)
        return 0;
    for (widget = petersburg_widgets; widget != NULL; widget = widget->next)
        if (widget->type == PETERSBURG_WINDOW)
            return 1;
#endif
    return 0;
}

void ussr_petersburg_cleanup(void)
{
#ifdef USSR_WITH_GLFW
    while (petersburg_widgets != NULL)
        (void)petersburg_destroy(petersburg_widgets->id);
    while (petersburg_font_count > 0)
    {
        --petersburg_font_count;
        free(petersburg_fonts[petersburg_font_count].data);
        free(petersburg_font_paths[petersburg_font_count]);
    }
    if (petersburg_initialized)
        glfwTerminate();
    petersburg_initialized = 0;
    petersburg_run_requested = 0;
    petersburg_event_head = 0;
    petersburg_event_count = 0;
    petersburg_focus = NULL;
    petersburg_last_error = 0;
    petersburg_events_delegated = 0;
#endif
}
