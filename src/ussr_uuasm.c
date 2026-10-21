#include "ussr_uuasm.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    uint32_t address;
} uuasm_label_t;

typedef struct
{
    uint32_t instruction;
    char *name;
    unsigned line;
} uuasm_fixup_t;

typedef struct
{
    char *name;
    char *entry_label;
    char *return_name;
    char **parameters;
    size_t parameter_count;
    unsigned line;
} uuasm_function_t;

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} uuasm_tokens_t;

typedef struct
{
    const char *name;
    uint8_t opcode;
} uuasm_opcode_name_t;

static const uuasm_opcode_name_t uuasm_opcodes[] =
{
    {"NOP", USSR_BC_NOP},
    {"LOAD_CONST", USSR_BC_LOAD_CONST},
    {"LOAD_VAR", USSR_BC_LOAD_VAR},
    {"STORE_VAR", USSR_BC_STORE_VAR},
    {"LOAD_HASH", USSR_BC_LOAD_HASH},
    {"STORE_HASH", USSR_BC_STORE_HASH},
    {"DECODE_UNO", USSR_BC_DECODE_UNO},
    {"GET", USSR_BC_GET},
    {"VECTOR_GET", USSR_BC_VECTOR_GET},
    {"VECTOR_SET", USSR_BC_VECTOR_SET},
    {"ADD", USSR_BC_ADD},
    {"SUB", USSR_BC_SUB},
    {"MUL", USSR_BC_MUL},
    {"DIV", USSR_BC_DIV},
    {"MOD", USSR_BC_MOD},
    {"NEG", USSR_BC_NEG},
    {"EQ", USSR_BC_EQ},
    {"NE", USSR_BC_NE},
    {"LT", USSR_BC_LT},
    {"LE", USSR_BC_LE},
    {"GT", USSR_BC_GT},
    {"GE", USSR_BC_GE},
    {"LAND", USSR_BC_LAND},
    {"LOR", USSR_BC_LOR},
    {"XOR", USSR_BC_XOR},
    {"BAND", USSR_BC_BAND},
    {"BOR", USSR_BC_BOR},
    {"SHL", USSR_BC_SHL},
    {"SHR", USSR_BC_SHR},
    {"JMP", USSR_BC_JMP},
    {"JMP_TRUE", USSR_BC_JMP_TRUE},
    {"JMP_FALSE", USSR_BC_JMP_FALSE},
    {"SET", USSR_BC_SET},
    {"PRINT", USSR_BC_PRINT},
    {"CONCAT", USSR_BC_CONCAT},
    {"EXTERNAL", USSR_BC_EXTERNAL},
    {"OOP", USSR_BC_OOP},
    {"EVAL", USSR_BC_EVAL},
    {"RANDOM64", USSR_BC_RANDOM64},
    {"SEED64", USSR_BC_SEED64},
    {"SCAN", USSR_BC_SCAN},
    {"TIME", USSR_BC_TIME},
    {"CHAIN", USSR_BC_CHAIN},
    {"FILE", USSR_BC_FILE},
    {"CD", USSR_BC_CD},
    {"TEMPLATE", USSR_BC_TEMPLATE},
    {"CALL", USSR_BC_CALL},
    {"RET", USSR_BC_RET},
    {"RETURN", USSR_BC_RETURN},
    {"HALT", USSR_BC_HALT}
};

int ussr_uuasm_to_vm_opcode(uint8_t uuasm_opcode, uint8_t *vm_opcode)
{
    size_t i;

    if (vm_opcode == NULL)
        return -1;

    for (i = 0; i < sizeof(uuasm_opcodes) / sizeof(uuasm_opcodes[0]); ++i)
    {
        if (uuasm_opcodes[i].opcode == uuasm_opcode)
        {
            *vm_opcode = uuasm_opcode;
            return 0;
        }
    }

    return -1;
}

static int uuasm_opcode_from_name(const char *name, uint8_t *opcode)
{
    size_t i;

    for (i = 0; i < sizeof(uuasm_opcodes) / sizeof(uuasm_opcodes[0]); ++i)
    {
        if (strcmp(name, uuasm_opcodes[i].name) == 0)
        {
            *opcode = uuasm_opcodes[i].opcode;
            return 0;
        }
    }

    return -1;
}

static void uuasm_tokens_free(uuasm_tokens_t *tokens)
{
    size_t i;

    for (i = 0; i < tokens->count; ++i)
        free(tokens->items[i]);

    free(tokens->items);
    memset(tokens, 0, sizeof(*tokens));
}

static int uuasm_token_add(
    uuasm_tokens_t *tokens,
    const char *start,
    size_t length
)
{
    char **items;
    char *text;
    size_t capacity;

    if (tokens->count == tokens->capacity)
    {
        capacity = tokens->capacity == 0 ? 8 : tokens->capacity * 2;
        items = realloc(tokens->items, capacity * sizeof(*items));
        if (items == NULL)
            return -1;
        tokens->items = items;
        tokens->capacity = capacity;
    }

    text = malloc(length + 1);
    if (text == NULL)
        return -1;
    memcpy(text, start, length);
    text[length] = '\0';
    tokens->items[tokens->count++] = text;
    return 0;
}

static int uuasm_tokenize(const char *line, uuasm_tokens_t *tokens)
{
    const char *p = line;

    memset(tokens, 0, sizeof(*tokens));
    while (*p != '\0')
    {
        const char *start;
        char *quoted = NULL;
        size_t used = 0;
        size_t capacity = 0;

        while (isspace((unsigned char)*p))
            ++p;
        if (*p == '\0' || *p == '#' || *p == ';')
            break;

        if (*p == '"')
        {
            int closed = 0;
            ++p;
            while (*p != '\0')
            {
                char c = *p++;

                if (c == '"')
                {
                    closed = 1;
                    break;
                }
                if (c == '\\')
                {
                    c = *p++;
                    if (c == '\0')
                        break;
                    if (c == 'n') c = '\n';
                    else if (c == 'r') c = '\r';
                    else if (c == 't') c = '\t';
                    else if (c != '\\' && c != '"')
                    {
                        free(quoted);
                        uuasm_tokens_free(tokens);
                        return -1;
                    }
                }

                if (used + 1 >= capacity)
                {
                    size_t next_capacity = capacity == 0 ? 32 : capacity * 2;
                    char *next = realloc(quoted, next_capacity);
                    if (next == NULL)
                    {
                        free(quoted);
                        uuasm_tokens_free(tokens);
                        return -1;
                    }
                    quoted = next;
                    capacity = next_capacity;
                }
                quoted[used++] = c;
            }
            if (!closed)
            {
                free(quoted);
                uuasm_tokens_free(tokens);
                return -1;
            }
            if (uuasm_token_add(tokens, quoted != NULL ? quoted : "", used) != 0)
            {
                free(quoted);
                uuasm_tokens_free(tokens);
                return -1;
            }
            free(quoted);
            if (*p != '\0' && !isspace((unsigned char)*p) &&
                *p != '#' && *p != ';')
            {
                uuasm_tokens_free(tokens);
                return -1;
            }
        }
        else
        {
            start = p;
            while (*p != '\0' && !isspace((unsigned char)*p) &&
                   *p != '#' && *p != ';')
                ++p;
            if (uuasm_token_add(tokens, start, (size_t)(p - start)) != 0)
            {
                uuasm_tokens_free(tokens);
                return -1;
            }
            if (*p == '#' || *p == ';')
                break;
        }
    }
    return 0;
}

static int uuasm_parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-')
        return -1;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int uuasm_parse_field(const char *text, uint8_t *value)
{
    uint32_t parsed;

    if (strcmp(text, "_") == 0)
    {
        *value = 0;
        return 0;
    }
    if ((text[0] == 'R' || text[0] == 'r') &&
        uuasm_parse_u32(text + 1, &parsed) == 0)
    {
        if (parsed >= USSR_BC_MAX_REGS)
            return -1;
        *value = (uint8_t)parsed;
        return 0;
    }
    if (uuasm_parse_u32(text, &parsed) != 0 || parsed > UINT8_MAX)
        return -1;
    *value = (uint8_t)parsed;
    return 0;
}

static int uuasm_grow(void **items, size_t *capacity, size_t count, size_t size)
{
    size_t next_capacity;
    void *next;

    if (count < *capacity)
        return 0;
    next_capacity = *capacity == 0 ? 16 : *capacity * 2;
    if (next_capacity < *capacity || next_capacity > SIZE_MAX / size)
        return -1;
    next = realloc(*items, next_capacity * size);
    if (next == NULL)
        return -1;
    *items = next;
    *capacity = next_capacity;
    return 0;
}

static int uuasm_add_label(
    uuasm_label_t **labels,
    size_t *count,
    size_t *capacity,
    const char *name,
    uint32_t address
)
{
    size_t i;
    char *copy;

    for (i = 0; i < *count; ++i)
        if (strcmp((*labels)[i].name, name) == 0)
            return -1;
    if (uuasm_grow((void **)labels, capacity, *count, sizeof(**labels)) != 0)
        return -1;
    copy = malloc(strlen(name) + 1);
    if (copy == NULL)
        return -1;
    strcpy(copy, name);
    (*labels)[*count].name = copy;
    (*labels)[*count].address = address;
    ++*count;
    return 0;
}

static int uuasm_find_label(
    const uuasm_label_t *labels,
    size_t count,
    const char *name,
    uint32_t *address
)
{
    size_t i;

    for (i = 0; i < count; ++i)
    {
        if (strcmp(labels[i].name, name) == 0)
        {
            *address = labels[i].address;
            return 0;
        }
    }
    return -1;
}

static int uuasm_add_fixup(
    uuasm_fixup_t **fixups,
    size_t *count,
    size_t *capacity,
    uint32_t instruction,
    const char *name,
    unsigned line
)
{
    char *copy;

    if (uuasm_grow((void **)fixups, capacity, *count, sizeof(**fixups)) != 0)
        return -1;
    copy = malloc(strlen(name) + 1);
    if (copy == NULL)
        return -1;
    strcpy(copy, name);
    (*fixups)[*count].instruction = instruction;
    (*fixups)[*count].name = copy;
    (*fixups)[*count].line = line;
    ++*count;
    return 0;
}

static int uuasm_add_string_at(
    ussr_uuasm_program_t *program,
    uint32_t index,
    const char *text
)
{
    char **strings;
    char *copy;
    size_t capacity;

    if (index != program->string_count ||
        program->string_count >= USSR_BC_MAX_CODE)
        return -1;
    if (program->string_count == program->string_capacity)
    {
        capacity = program->string_capacity == 0 ? 16 :
            program->string_capacity * 2;
        strings = realloc(program->strings, capacity * sizeof(*strings));
        if (strings == NULL)
            return -1;
        program->strings = strings;
        program->string_capacity = capacity;
    }
    copy = malloc(strlen(text) + 1);
    if (copy == NULL)
        return -1;
    strcpy(copy, text);
    program->strings[program->string_count++] = copy;
    return 0;
}

static int uuasm_add_constant_at(
    ussr_uuasm_program_t *program,
    uint32_t index,
    ussr_value_t value
)
{
    ussr_value_t *constants;
    size_t capacity;

    if (index != program->constant_count ||
        program->constant_count >= USSR_BC_MAX_CODE)
    {
        ussr_value_free(&value);
        return -1;
    }
    if (program->constant_count == program->constant_capacity)
    {
        capacity = program->constant_capacity == 0 ? 16 :
            program->constant_capacity * 2;
        constants = realloc(program->constants, capacity * sizeof(*constants));
        if (constants == NULL)
        {
            ussr_value_free(&value);
            return -1;
        }
        program->constants = constants;
        program->constant_capacity = capacity;
    }
    program->constants[program->constant_count++] = value;
    return 0;
}

static int uuasm_parse_constant(
    ussr_uuasm_program_t *program,
    const uuasm_tokens_t *tokens
)
{
    uint32_t index;
    char *end;
    ussr_int_t integer;
    double real;
    ussr_value_t value;

    if (tokens->count < 3 || tokens->count > 4 ||
        uuasm_parse_u32(tokens->items[1], &index) != 0)
        return -1;
    if (strcmp(tokens->items[2], "null") == 0)
    {
        if (tokens->count != 3)
            return -1;
        return uuasm_add_constant_at(program, index, ussr_null());
    }
    if (tokens->count != 4)
        return -1;
    if (strcmp(tokens->items[2], "int") == 0)
    {
        errno = 0;
        integer = strtoll(tokens->items[3], &end, 10);
        if (errno != 0 || *end != '\0')
            return -1;
        value = ussr_integer(integer);
    }
    else if (strcmp(tokens->items[2], "real") == 0)
    {
        errno = 0;
        real = strtod(tokens->items[3], &end);
        if (errno != 0 || *end != '\0')
            return -1;
        value = ussr_real(real);
    }
    else if (strcmp(tokens->items[2], "bool") == 0)
    {
        if (strcmp(tokens->items[3], "true") == 0)
            value = ussr_boolean(1);
        else if (strcmp(tokens->items[3], "false") == 0)
            value = ussr_boolean(0);
        else
            return -1;
    }
    else if (strcmp(tokens->items[2], "string") == 0)
    {
        value = ussr_string(tokens->items[3]);
        if (value.type != USSR_STRING)
            return -1;
    }
    else
    {
        return -1;
    }

    return uuasm_add_constant_at(program, index, value);
}

static void uuasm_functions_free(
    uuasm_function_t *functions,
    size_t count
)
{
    size_t i, j;

    for (i = 0; i < count; ++i)
    {
        free(functions[i].name);
        free(functions[i].entry_label);
        free(functions[i].return_name);
        for (j = 0; j < functions[i].parameter_count; ++j)
            free(functions[i].parameters[j]);
        free(functions[i].parameters);
    }
    free(functions);
}

static char *uuasm_copy_text(const char *text)
{
    char *copy = malloc(strlen(text) + 1);
    if (copy != NULL)
        strcpy(copy, text);
    return copy;
}

static int uuasm_parse_function(
    const uuasm_tokens_t *tokens,
    uuasm_function_t **functions,
    size_t *count,
    size_t *capacity,
    unsigned line
)
{
    uuasm_function_t function;
    uint32_t parameter_count;
    size_t i;

    memset(&function, 0, sizeof(function));
    if (tokens->count < 5 ||
        uuasm_parse_u32(tokens->items[4], &parameter_count) != 0 ||
        parameter_count > USSR_BC_RETURN_REG ||
        tokens->count != (size_t)parameter_count + 5 ||
        tokens->items[1][0] == '\0' ||
        tokens->items[3][0] == '\0' ||
        tokens->items[2][0] != '@' || tokens->items[2][1] == '\0')
        return -1;

    function.name = uuasm_copy_text(tokens->items[1]);
    function.entry_label = uuasm_copy_text(tokens->items[2] + 1);
    function.return_name = uuasm_copy_text(tokens->items[3]);
    function.parameter_count = parameter_count;
    function.line = line;
    function.parameters = calloc(parameter_count, sizeof(*function.parameters));
    if (function.name == NULL || function.entry_label == NULL ||
        function.return_name == NULL ||
        (parameter_count != 0 && function.parameters == NULL))
        goto fail;

    for (i = 0; i < parameter_count; ++i)
    {
        if (tokens->items[5 + i][0] == '\0')
            goto fail;
        function.parameters[i] = uuasm_copy_text(tokens->items[5 + i]);
        if (function.parameters[i] == NULL)
            goto fail;
    }
    for (i = 0; i < *count; ++i)
    {
        if (strcmp((*functions)[i].name, function.name) == 0)
            goto fail;
    }
    if (uuasm_grow(
            (void **)functions, capacity, *count, sizeof(**functions)) != 0)
        goto fail;

    (*functions)[(*count)++] = function;
    return 0;

fail:
    free(function.name);
    free(function.entry_label);
    free(function.return_name);
    for (i = 0; i < function.parameter_count; ++i)
        free(function.parameters != NULL ? function.parameters[i] : NULL);
    free(function.parameters);
    return -1;
}

static void uuasm_clear(ussr_uuasm_program_t *program)
{
    ussr_bc_program_free(program);
}

int ussr_uuasm_assemble(
    const char *source,
    ussr_uuasm_program_t *program
)
{
    uuasm_label_t *labels = NULL;
    uuasm_fixup_t *fixups = NULL;
    uuasm_function_t *functions = NULL;
    size_t label_count = 0, label_capacity = 0;
    size_t fixup_count = 0, fixup_capacity = 0;
    size_t function_count = 0, function_capacity = 0;
    const char *line_start;
    unsigned line_number = 1;
    int header_seen = 0;
    int result = -1;

    if (source == NULL || program == NULL)
        return -1;
    memset(program, 0, sizeof(*program));
    line_start = source;

    while (*line_start != '\0')
    {
        const char *line_end = strchr(line_start, '\n');
        size_t line_length = line_end != NULL ?
            (size_t)(line_end - line_start) : strlen(line_start);
        char *line;
        uuasm_tokens_t tokens;

        if (line_length > 65535)
            goto cleanup;
        line = malloc(line_length + 1);
        if (line == NULL)
            goto cleanup;
        memcpy(line, line_start, line_length);
        line[line_length] = '\0';
        if (uuasm_tokenize(line, &tokens) != 0)
        {
            fprintf(stderr, "UUASM:%u: malformed quoted token\n", line_number);
            free(line);
            goto cleanup;
        }

        if (tokens.count == 1)
        {
            size_t token_length = strlen(tokens.items[0]);
            if (token_length > 1 && tokens.items[0][token_length - 1] == ':')
            {
                if (!header_seen)
                    goto invalid_directive;
                tokens.items[0][token_length - 1] = '\0';
                if (tokens.items[0][0] == '\0' ||
                    uuasm_add_label(
                        &labels, &label_count, &label_capacity,
                        tokens.items[0], (uint32_t)program->code_count) != 0)
                {
                    fprintf(stderr, "UUASM:%u: duplicate or invalid label\n",
                        line_number);
                    uuasm_tokens_free(&tokens);
                    free(line);
                    goto cleanup;
                }
            }
        }
        else if (tokens.count > 0 && strcmp(tokens.items[0], ".const") == 0)
        {
            if (!header_seen)
                goto invalid_directive;
            if (uuasm_parse_constant(program, &tokens) != 0)
            {
                goto invalid_directive;
            }
        }
        else if (tokens.count > 0 && strcmp(tokens.items[0], ".string") == 0)
        {
            uint32_t index;
            if (!header_seen || tokens.count != 3 ||
                uuasm_parse_u32(tokens.items[1], &index) != 0 ||
                uuasm_add_string_at(program, index, tokens.items[2]) != 0)
            {
                goto invalid_directive;
            }
        }
        else if (tokens.count > 0 && strcmp(tokens.items[0], ".function") == 0)
        {
            if (!header_seen ||
                uuasm_parse_function(
                    &tokens, &functions, &function_count,
                    &function_capacity, line_number) != 0)
                goto invalid_directive;
        }
        else if (tokens.count > 0 && strcmp(tokens.items[0], ".uuasm") == 0)
        {
            if (header_seen || tokens.count != 2 ||
                strcmp(tokens.items[1], "1") != 0)
                goto invalid_directive;
            header_seen = 1;
        }
        else if (tokens.count > 0)
        {
            ussr_uuasm_instruction_t instruction;
            uint8_t vm_opcode;
            uint32_t immediate;

            if (!header_seen || program->code_count >= USSR_BC_MAX_CODE ||
                tokens.count != 5 ||
                uuasm_opcode_from_name(tokens.items[0], &instruction.opcode) != 0 ||
                ussr_uuasm_to_vm_opcode(instruction.opcode, &vm_opcode) != 0 ||
                uuasm_parse_field(tokens.items[1], &instruction.a) != 0 ||
                uuasm_parse_field(tokens.items[2], &instruction.b) != 0 ||
                uuasm_parse_field(tokens.items[3], &instruction.c) != 0)
            {
                goto invalid_instruction;
            }
            if (tokens.items[4][0] == '@')
            {
                if (instruction.opcode != USSR_BC_JMP &&
                    instruction.opcode != USSR_BC_JMP_TRUE &&
                    instruction.opcode != USSR_BC_JMP_FALSE)
                {
                    goto invalid_instruction;
                }
                if (uuasm_add_fixup(
                        &fixups, &fixup_count, &fixup_capacity,
                        (uint32_t)program->code_count,
                        tokens.items[4] + 1, line_number) != 0)
                {
                    goto cleanup_line;
                }
                immediate = 0;
            }
            else if (uuasm_parse_u32(tokens.items[4], &immediate) != 0)
            {
                goto invalid_instruction;
            }
            instruction.opcode = vm_opcode;
            instruction.immediate = immediate;

            if (uuasm_grow(
                    (void **)&program->code, &program->code_capacity,
                    program->code_count, sizeof(*program->code)) != 0)
            {
                goto cleanup_line;
            }
            program->code[program->code_count++] = instruction;
        }

        uuasm_tokens_free(&tokens);
        free(line);
        if (line_end == NULL)
            break;
        line_start = line_end + 1;
        ++line_number;
        continue;

invalid_directive:
        fprintf(stderr, "UUASM:%u: invalid directive\n", line_number);
        uuasm_tokens_free(&tokens);
        free(line);
        goto cleanup;

invalid_instruction:
        fprintf(stderr, "UUASM:%u: invalid instruction or operand\n",
            line_number);
        uuasm_tokens_free(&tokens);
        free(line);
        goto cleanup;

cleanup_line:
        uuasm_tokens_free(&tokens);
        free(line);
        goto cleanup;
    }

    if (!header_seen)
    {
        fprintf(stderr, "UUASM: missing .uuasm 1 header\n");
        goto cleanup;
    }

    for (line_number = 0; line_number < fixup_count; ++line_number)
    {
        uint32_t target;
        uuasm_fixup_t *fixup = &fixups[line_number];
        if (uuasm_find_label(
                labels, label_count, fixup->name, &target) != 0 ||
            target >= program->code_count)
        {
            fprintf(stderr, "UUASM:%u: unresolved or out-of-range label '%s'\n",
                fixup->line, fixup->name);
            goto cleanup;
        }
        program->code[fixup->instruction].immediate = target;
    }

    if (function_count > 0)
    {
        size_t i, j;
        program->functions = calloc(function_count, sizeof(*program->functions));
        if (program->functions == NULL)
            goto cleanup;
        program->function_capacity = function_count;
        for (i = 0; i < function_count; ++i)
        {
            uint32_t entry;
            program->function_count = i + 1;
            if (uuasm_find_label(
                    labels, label_count, functions[i].entry_label, &entry) != 0 ||
                entry >= program->code_count)
            {
                fprintf(stderr,
                        "UUASM:%u: unresolved function entry '%s'\n",
                        functions[i].line, functions[i].entry_label);
                goto cleanup;
            }
            program->functions[i].name =
                uuasm_copy_text(functions[i].name);
            program->functions[i].return_name =
                uuasm_copy_text(functions[i].return_name);
            program->functions[i].parameters =
                calloc(functions[i].parameter_count,
                       sizeof(*program->functions[i].parameters));
            program->functions[i].parameter_count =
                functions[i].parameter_count;
            if (program->functions[i].name == NULL ||
                program->functions[i].return_name == NULL ||
                (functions[i].parameter_count != 0 &&
                 program->functions[i].parameters == NULL))
                goto cleanup;
            for (j = 0; j < functions[i].parameter_count; ++j)
            {
                program->functions[i].parameters[j] =
                    uuasm_copy_text(functions[i].parameters[j]);
                if (program->functions[i].parameters[j] == NULL)
                    goto cleanup;
            }
            program->functions[i].entry = entry;
            /* UUASM text has no local declarations: parameters and the
             * return variable are local, every other name is global. */
            if (ussr_bc_function_default_locals(&program->functions[i]) != 0)
                goto cleanup;
        }
    }

    if (program->code_count == 0 ||
        program->code[program->code_count - 1].opcode != USSR_BC_HALT)
    {
        fprintf(stderr, "UUASM: program must end with HALT\n");
        goto cleanup;
    }

    result = 0;

cleanup:
    for (line_number = 0; line_number < label_count; ++line_number)
        free(labels[line_number].name);
    for (line_number = 0; line_number < fixup_count; ++line_number)
        free(fixups[line_number].name);
    free(labels);
    free(fixups);
    uuasm_functions_free(functions, function_count);
    if (result != 0)
        uuasm_clear(program);
    return result;
}
