#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tula.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TULA_CANVAS_WIDTH 1001
#define TULA_CANVAS_HEIGHT 701
#define TULA_GRID_STEP 5.0
#define TULA_PIXELS_PER_UNIT 5.0
#define TULA_MAX_ENTITIES 10000

typedef enum
{
    TULA_ENTITY_LINE,
    TULA_ENTITY_CIRCLE
} tula_entity_type_t;

typedef struct
{
    tula_entity_type_t type;
    double x1;
    double y1;
    double x2;
    double y2;
    double radius;
} tula_entity_t;

typedef enum
{
    TULA_TOOL_NONE,
    TULA_TOOL_LINE,
    TULA_TOOL_CIRCLE
} tula_tool_t;

typedef struct
{
    long window;
    long status;
    long path_widget;
    long canvas;
    char canvas_path[PATH_MAX];
    tula_entity_t *entities;
    size_t entity_count;
    size_t entity_capacity;
    tula_tool_t tool;
    int has_anchor;
    double anchor_x;
    double anchor_y;
} tula_state_t;

static tula_state_t tula_state;

static int tula_integer(
    const ussr_value_t *values,
    size_t count,
    size_t index,
    long *result
)
{
    if (values == NULL || index >= count ||
        values[index].type != USSR_INTEGER)
        return 0;
    *result = values[index].data.integer;
    return 1;
}

static int tula_string(
    const ussr_value_t *values,
    size_t count,
    size_t index,
    const char **result
)
{
    if (values == NULL || index >= count ||
        values[index].type != USSR_STRING ||
        values[index].data.string == NULL)
        return 0;
    *result = values[index].data.string;
    return 1;
}

static int tula_arity(const char *name, size_t actual, size_t expected)
{
    if (actual == expected)
        return 1;
    fprintf(stderr, "USSR: %s expects %lu argument(s), got %lu\n",
            name, (unsigned long)expected, (unsigned long)actual);
    return 0;
}

static int tula_petersburg_call(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *arguments,
    size_t argument_count,
    ussr_value_t *result
)
{
    return ussr_petersburg_dispatch(
        program, name, arguments, argument_count, result
    ) == 1;
}

static int tula_set_status(
    ussr_sched_program_t *program,
    const char *text
)
{
    ussr_value_t arguments[2];
    ussr_value_t result = ussr_null();
    int ok;

    arguments[0] = ussr_integer(tula_state.status);
    arguments[1] = ussr_string(text);
    if (arguments[1].data.string == NULL)
    {
        ussr_value_free(&arguments[1]);
        return 0;
    }
    ok = tula_petersburg_call(program, "petersburg_set_text",
                              arguments, 2, &result);
    ussr_value_free(&arguments[1]);
    ussr_value_free(&result);
    return ok;
}

static void tula_pixel(
    unsigned char *pixels,
    int x,
    int y,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    size_t offset;

    if (x < 0 || x >= TULA_CANVAS_WIDTH ||
        y < 0 || y >= TULA_CANVAS_HEIGHT)
        return;
    offset = ((size_t)y * TULA_CANVAS_WIDTH + (size_t)x) * 3;
    pixels[offset] = red;
    pixels[offset + 1] = green;
    pixels[offset + 2] = blue;
}

static void tula_line_pixels(
    unsigned char *pixels,
    int x0,
    int y0,
    int x1,
    int y1,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;)
    {
        tula_pixel(pixels, x0, y0, red, green, blue);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int doubled = error * 2;
            if (doubled >= dy)
            {
                error += dy;
                x0 += sx;
            }
            if (doubled <= dx)
            {
                error += dx;
                y0 += sy;
            }
        }
    }
}

static void tula_circle_pixels(
    unsigned char *pixels,
    int center_x,
    int center_y,
    int radius,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int x = radius;
    int y = 0;
    int error = 1 - radius;

    while (x >= y)
    {
        tula_pixel(pixels, center_x + x, center_y + y, red, green, blue);
        tula_pixel(pixels, center_x + y, center_y + x, red, green, blue);
        tula_pixel(pixels, center_x - y, center_y + x, red, green, blue);
        tula_pixel(pixels, center_x - x, center_y + y, red, green, blue);
        tula_pixel(pixels, center_x - x, center_y - y, red, green, blue);
        tula_pixel(pixels, center_x - y, center_y - x, red, green, blue);
        tula_pixel(pixels, center_x + y, center_y - x, red, green, blue);
        tula_pixel(pixels, center_x + x, center_y - y, red, green, blue);
        ++y;
        if (error < 0)
            error += 2 * y + 1;
        else
        {
            --x;
            error += 2 * (y - x + 1);
        }
    }
}

static int tula_pixel_x(double x)
{
    return (int)lround(x * TULA_PIXELS_PER_UNIT);
}

static int tula_pixel_y(double y)
{
    return TULA_CANVAS_HEIGHT - 1 -
           (int)lround(y * TULA_PIXELS_PER_UNIT);
}

static void tula_draw_grid(unsigned char *pixels)
{
    int x;
    int y;

    memset(pixels, 250,
           (size_t)TULA_CANVAS_WIDTH * TULA_CANVAS_HEIGHT * 3);
    for (x = 0; x < TULA_CANVAS_WIDTH; x += 25)
    {
        unsigned char shade = x % 100 == 0 ? 220 : 237;
        for (y = 0; y < TULA_CANVAS_HEIGHT; ++y)
            tula_pixel(pixels, x, y, shade, shade, shade);
    }
    for (y = 0; y < TULA_CANVAS_HEIGHT; y += 25)
    {
        unsigned char shade = y % 100 == 0 ? 220 : 237;
        for (x = 0; x < TULA_CANVAS_WIDTH; ++x)
            tula_pixel(pixels, x, y, shade, shade, shade);
    }
    for (x = 0; x < TULA_CANVAS_WIDTH; x += 100)
        for (y = 0; y < TULA_CANVAS_HEIGHT; ++y)
            tula_pixel(pixels, x, y, 220, 220, 220);
    for (y = 0; y < TULA_CANVAS_HEIGHT; y += 100)
        for (x = 0; x < TULA_CANVAS_WIDTH; ++x)
            tula_pixel(pixels, x, y, 220, 220, 220);
}

static int tula_write_canvas(void)
{
    FILE *file;
    unsigned char *pixels;
    size_t index;
    int ok;

    pixels = malloc((size_t)TULA_CANVAS_WIDTH * TULA_CANVAS_HEIGHT * 3);
    if (pixels == NULL)
        return 0;
    tula_draw_grid(pixels);
    for (index = 0; index < tula_state.entity_count; ++index)
    {
        const tula_entity_t *entity = &tula_state.entities[index];
        if (entity->type == TULA_ENTITY_LINE)
            tula_line_pixels(pixels,
                tula_pixel_x(entity->x1), tula_pixel_y(entity->y1),
                tula_pixel_x(entity->x2), tula_pixel_y(entity->y2),
                24, 73, 132);
        else
            tula_circle_pixels(pixels,
                tula_pixel_x(entity->x1), tula_pixel_y(entity->y1),
                (int)lround(entity->radius * TULA_PIXELS_PER_UNIT),
                24, 73, 132);
    }
    if (tula_state.has_anchor)
    {
        int px = tula_pixel_x(tula_state.anchor_x);
        int py = tula_pixel_y(tula_state.anchor_y);
        tula_line_pixels(pixels, px - 5, py, px + 5, py,
                         204, 69, 55);
        tula_line_pixels(pixels, px, py - 5, px, py + 5,
                         204, 69, 55);
    }
    file = fopen(tula_state.canvas_path, "wb");
    if (file == NULL)
    {
        free(pixels);
        return 0;
    }
    ok = fprintf(file, "P6\n%d %d\n255\n",
                 TULA_CANVAS_WIDTH, TULA_CANVAS_HEIGHT) > 0 &&
         fwrite(pixels, 1,
                (size_t)TULA_CANVAS_WIDTH * TULA_CANVAS_HEIGHT * 3,
                file) == (size_t)TULA_CANVAS_WIDTH * TULA_CANVAS_HEIGHT * 3;
    if (fclose(file) != 0)
        ok = 0;
    free(pixels);
    return ok;
}

static int tula_refresh_canvas(ussr_sched_program_t *program)
{
    ussr_value_t arguments[2];
    ussr_value_t result = ussr_null();
    int ok;

    if (!tula_write_canvas())
    {
        fprintf(stderr, "USSR: Tula could not render its drawing canvas\n");
        return 0;
    }
    arguments[0] = ussr_integer(tula_state.canvas);
    arguments[1] = ussr_string(tula_state.canvas_path);
    if (arguments[1].data.string == NULL)
    {
        ussr_value_free(&arguments[1]);
        return 0;
    }
    ok = tula_petersburg_call(program, "petersburg_reload_image",
                              arguments, 2, &result);
    ussr_value_free(&arguments[1]);
    ussr_value_free(&result);
    return ok;
}

static int tula_create_temp_canvas(void)
{
#ifdef _WIN32
    char directory[MAX_PATH];
    DWORD length = GetTempPathA((DWORD)sizeof(directory), directory);
    if (length == 0 || length >= sizeof(directory) ||
        GetTempFileNameA(directory, "tul", 0, tula_state.canvas_path) == 0)
        return 0;
#else
    const char *temporary_directory = getenv("TMPDIR");
    int length;
    int descriptor;
    FILE *file;

    if (temporary_directory == NULL || temporary_directory[0] == '\0')
        temporary_directory = "/tmp";
    length = snprintf(tula_state.canvas_path,
                      sizeof(tula_state.canvas_path),
                      "%s/tula-canvas-XXXXXX", temporary_directory);
    if (length < 0 || (size_t)length >= sizeof(tula_state.canvas_path))
        return 0;
    descriptor = mkstemp(tula_state.canvas_path);
    if (descriptor < 0)
        return 0;
    file = fdopen(descriptor, "wb");
    if (file == NULL)
    {
        close(descriptor);
        remove(tula_state.canvas_path);
        tula_state.canvas_path[0] = '\0';
        return 0;
    }
    if (fclose(file) != 0)
    {
        remove(tula_state.canvas_path);
        tula_state.canvas_path[0] = '\0';
        return 0;
    }
#endif
    return 1;
}

static int tula_add_entity(const tula_entity_t *entity)
{
    tula_entity_t *grown;
    size_t capacity;

    if (tula_state.entity_count >= TULA_MAX_ENTITIES)
        return 0;
    if (tula_state.entity_count == tula_state.entity_capacity)
    {
        capacity = tula_state.entity_capacity == 0
                       ? 32 : tula_state.entity_capacity * 2;
        if (capacity > TULA_MAX_ENTITIES)
            capacity = TULA_MAX_ENTITIES;
        grown = realloc(tula_state.entities,
                        capacity * sizeof(*tula_state.entities));
        if (grown == NULL)
            return 0;
        tula_state.entities = grown;
        tula_state.entity_capacity = capacity;
    }
    tula_state.entities[tula_state.entity_count++] = *entity;
    return 1;
}

static double tula_snap(double coordinate)
{
    return floor(coordinate / TULA_GRID_STEP + 0.5) * TULA_GRID_STEP;
}

static int tula_save_dxf(
    ussr_sched_program_t *program
)
{
    ussr_value_t argument = ussr_integer(tula_state.path_widget);
    ussr_value_t result = ussr_null();
    const char *path;
    FILE *file;
    size_t index;
    int ok = 1;

    if (!tula_petersburg_call(program, "petersburg_get_text",
                              &argument, 1, &result) ||
        result.type != USSR_STRING ||
        result.data.string == NULL || result.data.string[0] == '\0')
    {
        ussr_value_free(&result);
        return tula_set_status(program,
                               "Enter a DXF filename before saving");
    }
    path = result.data.string;
    file = fopen(path, "w");
    if (file == NULL)
    {
        char message[512];
        snprintf(message, sizeof(message), "Cannot save DXF: %s",
                 strerror(errno));
        ok = tula_set_status(program, message);
        ussr_value_free(&result);
        return ok;
    }
    ok = fprintf(file,
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n"
        "9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n") >= 0;
    for (index = 0; ok && index < tula_state.entity_count; ++index)
    {
        const tula_entity_t *entity = &tula_state.entities[index];
        if (entity->type == TULA_ENTITY_LINE)
            ok = fprintf(file,
                "0\nLINE\n8\n0\n10\n%.10g\n20\n%.10g\n30\n0\n"
                "11\n%.10g\n21\n%.10g\n31\n0\n",
                entity->x1, entity->y1, entity->x2, entity->y2) >= 0;
        else
            ok = fprintf(file,
                "0\nCIRCLE\n8\n0\n10\n%.10g\n20\n%.10g\n30\n0\n40\n%.10g\n",
                entity->x1, entity->y1, entity->radius) >= 0;
    }
    if (ok)
        ok = fprintf(file, "0\nENDSEC\n0\nEOF\n") >= 0;
    if (fclose(file) != 0)
        ok = 0;
    if (ok)
    {
        char message[512];
        snprintf(message, sizeof(message), "Saved %lu object(s) to %s",
                 (unsigned long)tula_state.entity_count, path);
        ok = tula_set_status(program, message);
    }
    else
        ok = tula_set_status(program, "DXF write failed");
    ussr_value_free(&result);
    return ok;
}

static int tula_initialize(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count,
    ussr_value_t *out
)
{
    ussr_value_t image_arguments[2];
    ussr_value_t image_result = ussr_null();
    long handles[3];
    size_t index;
    int ok;

    if (!tula_arity("tula_initialize", count, 3))
        return 0;
    for (index = 0; index < 3; ++index)
        if (!tula_integer(values, count, index, &handles[index]))
        {
            fprintf(stderr,
                    "USSR: tula_initialize expects window, status, and filename widget handles\n");
            return 0;
        }

    ussr_tula_cleanup();
    tula_state.window = handles[0];
    tula_state.status = handles[1];
    tula_state.path_widget = handles[2];
    if (!tula_create_temp_canvas() || !tula_write_canvas())
    {
        fprintf(stderr, "USSR: Tula could not create its temporary canvas\n");
        ussr_tula_cleanup();
        return 0;
    }
    image_arguments[0] = ussr_integer(tula_state.window);
    image_arguments[1] = ussr_string(tula_state.canvas_path);
    if (image_arguments[1].data.string == NULL)
    {
        ussr_value_free(&image_arguments[1]);
        ussr_tula_cleanup();
        return 0;
    }
    ok = tula_petersburg_call(program, "petersburg_create_image",
                              image_arguments, 2, &image_result);
    ussr_value_free(&image_arguments[1]);
    if (!ok || image_result.type != USSR_INTEGER)
    {
        ussr_value_free(&image_result);
        ussr_tula_cleanup();
        return 0;
    }
    tula_state.canvas = image_result.data.integer;
    ussr_value_free(&image_result);
    if (!tula_set_status(program,
                         "LINE tool: click two snapped points; CIRCLE tool: center then radius; Save exports DXF"))
        return 0;
    *out = ussr_integer(tula_state.canvas);
    return 1;
}

static int tula_set_tool(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count
)
{
    const char *tool;
    if (!tula_arity("tula_set_tool", count, 1) ||
        !tula_string(values, count, 0, &tool))
    {
        fprintf(stderr, "USSR: tula_set_tool expects a tool name\n");
        return 0;
    }
    if (strcmp(tool, "line") == 0)
        tula_state.tool = TULA_TOOL_LINE;
    else if (strcmp(tool, "circle") == 0)
        tula_state.tool = TULA_TOOL_CIRCLE;
    else
    {
        fprintf(stderr, "USSR: Tula supports only line and circle tools\n");
        return 0;
    }
    tula_state.has_anchor = 0;
    return tula_set_status(program, tula_state.tool == TULA_TOOL_LINE
        ? "LINE tool: click the first snapped endpoint"
        : "CIRCLE tool: click the snapped center");
}

static int tula_canvas_click(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count
)
{
    long canvas;
    long pixel_x;
    long pixel_y;
    double x;
    double y;
    tula_entity_t entity;
    char message[256];

    if (!tula_arity("tula_canvas_click", count, 3) ||
        !tula_integer(values, count, 0, &canvas) ||
        !tula_integer(values, count, 1, &pixel_x) ||
        !tula_integer(values, count, 2, &pixel_y) ||
        canvas != tula_state.canvas ||
        pixel_x < 0 || pixel_x >= TULA_CANVAS_WIDTH ||
        pixel_y < 0 || pixel_y >= TULA_CANVAS_HEIGHT)
    {
        fprintf(stderr, "USSR: tula_canvas_click received invalid canvas coordinates\n");
        return 0;
    }
    if (tula_state.tool == TULA_TOOL_NONE)
        return tula_set_status(program, "Choose LINE or CIRCLE before drawing");
    x = tula_snap((double)pixel_x / TULA_PIXELS_PER_UNIT);
    y = tula_snap((double)(TULA_CANVAS_HEIGHT - 1 - pixel_y) /
                  TULA_PIXELS_PER_UNIT);
    if (!tula_state.has_anchor)
    {
        tula_state.anchor_x = x;
        tula_state.anchor_y = y;
        tula_state.has_anchor = 1;
        if (!tula_refresh_canvas(program))
            return tula_set_status(program, "Canvas preview refresh failed");
        snprintf(message, sizeof(message), "First point: %.0f, %.0f - click next point",
                 x, y);
        return tula_set_status(program, message);
    }
    entity.x1 = tula_state.anchor_x;
    entity.y1 = tula_state.anchor_y;
    entity.x2 = x;
    entity.y2 = y;
    if (tula_state.tool == TULA_TOOL_LINE)
    {
        if (entity.x1 == x && entity.y1 == y)
            return tula_set_status(program, "Line endpoints must be different");
        entity.type = TULA_ENTITY_LINE;
        entity.radius = 0;
    }
    else
    {
        entity.type = TULA_ENTITY_CIRCLE;
        entity.radius = hypot(x - entity.x1, y - entity.y1);
        if (entity.radius == 0.0)
            return tula_set_status(program, "Circle radius must be greater than zero");
    }
    if (!tula_add_entity(&entity))
        return tula_set_status(program, "Drawing is full or memory is unavailable");
    tula_state.has_anchor = 0;
    if (!tula_refresh_canvas(program))
        return tula_set_status(program, "Object added, but canvas refresh failed");
    snprintf(message, sizeof(message), "%s added at grid snap (%.0f, %.0f); %lu object(s)",
             entity.type == TULA_ENTITY_LINE ? "Line" : "Circle",
             entity.x1, entity.y1, (unsigned long)tula_state.entity_count);
    return tula_set_status(program, message);
}

int ussr_tula_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    if (name == NULL || strncmp(name, "tula_", 5) != 0)
        return 0;
    if (strcmp(name, "tula_initialize") == 0)
        return tula_initialize(program, values, value_count, out) ? 1 : -1;
    if (strcmp(name, "tula_set_tool") == 0)
        return tula_set_tool(program, values, value_count)
                   ? (*out = ussr_integer(0), 1) : -1;
    if (strcmp(name, "tula_canvas_click") == 0)
        return tula_canvas_click(program, values, value_count)
                   ? (*out = ussr_integer(0), 1) : -1;
    if (strcmp(name, "tula_save_dxf") == 0)
        return tula_arity(name, value_count, 0) &&
                       tula_save_dxf(program)
                   ? (*out = ussr_integer(0), 1) : -1;
    fprintf(stderr, "USSR: unknown Tula native command '%s'\n", name);
    return -1;
}

void ussr_tula_cleanup(void)
{
    free(tula_state.entities);
    if (tula_state.canvas_path[0] != '\0')
        remove(tula_state.canvas_path);
    memset(&tula_state, 0, sizeof(tula_state));
}
