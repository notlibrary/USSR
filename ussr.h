#ifndef USSR_H
#define USSR_H

#include <stddef.h>

typedef enum
{
    USSR_NULL,
    USSR_INTEGER,
    USSR_REAL,
    USSR_STRING,
    USSR_BOOLEAN
} ussr_value_type_t;

typedef struct
{
    ussr_value_type_t type;

    union
    {
        long integer;
        double real;
        char *string;
        int boolean;
    } data;

} ussr_value_t;

typedef struct
{
    char *name;
    ussr_value_t value;
} ussr_variable_t;

void ussr_init(void);
void ussr_cleanup(void);

ussr_value_t ussr_null(void);
ussr_value_t ussr_integer(long value);
ussr_value_t ussr_real(double value);
ussr_value_t ussr_boolean(int value);
ussr_value_t ussr_string(const char *value);

void ussr_value_free(ussr_value_t *value);
ussr_value_t ussr_value_copy(const ussr_value_t *value);

int ussr_set_variable(const char *name, const ussr_value_t *value);
const ussr_value_t *ussr_get_variable(const char *name);

int ussr_execute_command(
    const char *command,
    const char *return_name,
    ussr_value_t *parameters,
    size_t parameter_count
);

void ussr_print_value(const ussr_value_t *value);

#endif