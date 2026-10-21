/*
 * .ru raw bytecode writer and loader. The layout is documented in
 * docs/RU_BYTECODE_FORMAT.md; every multi-byte field is little-endian.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "ussr.h"
#include "ussr_bytecode.h"
#include "ussr_version.h"
#include "ussr_ru.h"

#define RU_HEADER_SIZE   24U
#define RU_NULL_STRING   0xffffffffU
#define RU_MAX_FILE_SIZE (256UL * 1024UL * 1024UL)

enum
{
    RU_CONST_NULL = 0,
    RU_CONST_INTEGER = 1,
    RU_CONST_REAL = 2,
    RU_CONST_STRING = 3,
    RU_CONST_BOOLEAN = 4
};

enum
{
    RU_ARG_VALUE = 0,
    RU_ARG_VARIABLE = 1
};

/* ------------------------------------------------------------- */
/* CRC-32 (IEEE 802.3, reflected, as used by zip and PNG)          */
/* ------------------------------------------------------------- */

static uint32_t ru_crc32(const unsigned char *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    size_t i;
    int bit;

    for (i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }

    return crc ^ 0xffffffffU;
}

/* ------------------------------------------------------------- */
/* writer                                                         */
/* ------------------------------------------------------------- */

typedef struct
{
    unsigned char *data;
    size_t length;
    size_t capacity;
    int failed;
} ru_buffer_t;

static void ru_put(ru_buffer_t *b, const void *bytes, size_t count)
{
    if (b->failed)
        return;

    if (b->length + count > b->capacity)
    {
        size_t next = b->capacity == 0 ? 4096 : b->capacity;
        unsigned char *grown;

        while (next < b->length + count)
            next *= 2;
        grown = realloc(b->data, next);
        if (grown == NULL)
        {
            b->failed = 1;
            return;
        }
        b->data = grown;
        b->capacity = next;
    }

    memcpy(b->data + b->length, bytes, count);
    b->length += count;
}

static void ru_put_u8(ru_buffer_t *b, unsigned value)
{
    unsigned char byte = (unsigned char)value;
    ru_put(b, &byte, 1);
}

static void ru_put_u16(ru_buffer_t *b, unsigned value)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8) & 0xffU);
    ru_put(b, bytes, 2);
}

static void ru_put_u32(ru_buffer_t *b, uint32_t value)
{
    unsigned char bytes[4];
    int i;
    for (i = 0; i < 4; ++i)
        bytes[i] = (unsigned char)((value >> (8 * i)) & 0xffU);
    ru_put(b, bytes, 4);
}

static void ru_put_u64(ru_buffer_t *b, uint64_t value)
{
    unsigned char bytes[8];
    int i;
    for (i = 0; i < 8; ++i)
        bytes[i] = (unsigned char)((value >> (8 * i)) & 0xffU);
    ru_put(b, bytes, 8);
}

/* Strings are a u32 byte length and the bytes; NULL is 0xffffffff. */
static void ru_put_string(ru_buffer_t *b, const char *text)
{
    size_t length;

    if (text == NULL)
    {
        ru_put_u32(b, RU_NULL_STRING);
        return;
    }

    length = strlen(text);
    if (length >= RU_NULL_STRING)
    {
        b->failed = 1;
        return;
    }
    ru_put_u32(b, (uint32_t)length);
    ru_put(b, text, length);
}

static int ru_put_constant(ru_buffer_t *b, const ussr_value_t *value)
{
    switch (value->type)
    {
        case USSR_NULL:
            ru_put_u8(b, RU_CONST_NULL);
            return 0;

        case USSR_INTEGER:
            ru_put_u8(b, RU_CONST_INTEGER);
            ru_put_u64(b, (uint64_t)(int64_t)value->data.integer);
            return 0;

        case USSR_REAL:
        {
            uint64_t bits;
            double real = value->data.real;
            memcpy(&bits, &real, sizeof(bits));
            ru_put_u8(b, RU_CONST_REAL);
            ru_put_u64(b, bits);
            return 0;
        }

        case USSR_STRING:
            ru_put_u8(b, RU_CONST_STRING);
            ru_put_string(b, value->data.string != NULL ?
                          value->data.string : "");
            return 0;

        case USSR_BOOLEAN:
            ru_put_u8(b, RU_CONST_BOOLEAN);
            ru_put_u8(b, value->data.boolean ? 1U : 0U);
            return 0;

        default:
            return -1;
    }
}

static int ru_put_site(ru_buffer_t *b, uint32_t instruction,
                       const ussr_command_t *command, const char *kind)
{
    size_t i;

    if (command == NULL)
    {
        fprintf(stderr, "USSR compile: %s site without source command\n",
                kind);
        return -1;
    }

    for (i = 0; i < command->argument_count; ++i)
    {
        ussr_argument_type_t type = command->arguments[i].type;
        if (type == USSR_ARGUMENT_COMMAND_LIST ||
            type == USSR_ARGUMENT_ADVANCED_LIST)
        {
            fprintf(stderr,
                    "USSR compile: '%s' is given a [ ... ] block as "
                    "argument %u; blocks passed to commands cannot be "
                    "stored in a .ru file\n",
                    command->name != NULL ? command->name : "?",
                    (unsigned)(i + 1));
            return -1;
        }
    }

    if (command->argument_count > 255)
        return -1;

    ru_put_u32(b, instruction);
    ru_put_string(b, command->name);
    ru_put_string(b, command->return_name);
    ru_put_u8(b, (unsigned)command->argument_count);

    for (i = 0; i < command->argument_count; ++i)
    {
        const ussr_argument_t *argument = &command->arguments[i];
        int is_variable =
            argument->type == USSR_ARGUMENT_EXPRESSION &&
            argument->data.expression != NULL &&
            argument->data.expression->type == USSR_EXPR_VARIABLE;

        ru_put_u8(b, is_variable ? RU_ARG_VARIABLE : RU_ARG_VALUE);
        ru_put_u8(b, argument->assignment ? 1U : 0U);
        if (is_variable)
            ru_put_string(b, argument->data.expression->data.variable);
    }

    return 0;
}

int ussr_ru_write_file(const ussr_bc_program_t *program, const char *path)
{
    ru_buffer_t b;
    FILE *output;
    size_t i;
    size_t j;
    uint32_t crc;
    int written;

    if (program == NULL || path == NULL)
        return -1;

    memset(&b, 0, sizeof(b));

    ru_put(&b, USSR_RU_MAGIC, USSR_RU_MAGIC_SIZE);
    ru_put_u16(&b, USSR_RU_FORMAT_VERSION);
    ru_put_u16(&b, 0);
    ru_put_u32(&b, USSR_RU_VM_ABI);
    ru_put_u32(&b, ((uint32_t)USSR_VERSION_MAJOR << 16) |
                   ((uint32_t)USSR_VERSION_MINOR << 8) |
                   (uint32_t)USSR_VERSION_PATCH);
    ru_put_u32(&b, 0); /* payload length, patched below */

    ru_put_u32(&b, (uint32_t)program->code_count);
    for (i = 0; i < program->code_count; ++i)
    {
        const ussr_bc_instruction_t *ins = &program->code[i];
        ru_put_u8(&b, ins->opcode);
        ru_put_u8(&b, ins->a);
        ru_put_u8(&b, ins->b);
        ru_put_u8(&b, ins->c);
        ru_put_u32(&b, ins->immediate);
    }

    ru_put_u32(&b, (uint32_t)program->constant_count);
    for (i = 0; i < program->constant_count; ++i)
    {
        if (ru_put_constant(&b, &program->constants[i]) != 0)
        {
            fprintf(stderr,
                    "USSR compile: constant %u has a type that cannot be "
                    "stored in a .ru file\n", (unsigned)i);
            free(b.data);
            return -1;
        }
    }

    ru_put_u32(&b, (uint32_t)program->string_count);
    for (i = 0; i < program->string_count; ++i)
        ru_put_string(&b, program->strings[i]);

    ru_put_u32(&b, (uint32_t)program->function_count);
    for (i = 0; i < program->function_count; ++i)
    {
        const ussr_bc_function_t *f = &program->functions[i];
        ru_put_string(&b, f->name);
        ru_put_string(&b, f->return_name);
        ru_put_u32(&b, f->entry);
        ru_put_u32(&b, (uint32_t)f->parameter_count);
        for (j = 0; j < f->parameter_count; ++j)
            ru_put_string(&b, f->parameters[j]);
        ru_put_u32(&b, (uint32_t)f->local_count);
        for (j = 0; j < f->local_count; ++j)
            ru_put_string(&b, f->locals[j]);
    }

    ru_put_u32(&b, (uint32_t)program->oop_site_count);
    for (i = 0; i < program->oop_site_count; ++i)
    {
        if (ru_put_site(&b, program->oop_sites[i].instruction,
                        program->oop_sites[i].command, "command") != 0)
        {
            free(b.data);
            return -1;
        }
    }

    ru_put_u32(&b, (uint32_t)program->scan_site_count);
    for (i = 0; i < program->scan_site_count; ++i)
    {
        if (ru_put_site(&b, program->scan_sites[i].instruction,
                        program->scan_sites[i].command, "scan") != 0)
        {
            free(b.data);
            return -1;
        }
    }

    if (b.failed)
    {
        fprintf(stderr, "USSR compile: out of memory\n");
        free(b.data);
        return -1;
    }

    {
        uint32_t payload = (uint32_t)(b.length - RU_HEADER_SIZE);
        for (i = 0; i < 4; ++i)
            b.data[20 + i] = (unsigned char)((payload >> (8 * i)) & 0xffU);
    }

    crc = ru_crc32(b.data, b.length);
    ru_put_u32(&b, crc);
    if (b.failed)
    {
        fprintf(stderr, "USSR compile: out of memory\n");
        free(b.data);
        return -1;
    }

    output = fopen(path, "wb");
    if (output == NULL)
    {
        fprintf(stderr, "USSR compile: cannot create '%s'\n", path);
        free(b.data);
        return -1;
    }

    written = fwrite(b.data, 1, b.length, output) == b.length;
    if (fclose(output) != 0)
        written = 0;
    free(b.data);

    if (!written)
    {
        fprintf(stderr, "USSR compile: cannot write '%s'\n", path);
        remove(path);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------- */
/* reader                                                         */
/* ------------------------------------------------------------- */

typedef struct
{
    const unsigned char *data;
    size_t length;
    size_t offset;
    const char *path;
    const char *error;
} ru_reader_t;

static int ru_fail(ru_reader_t *r, const char *message)
{
    if (r->error == NULL)
        r->error = message;
    return -1;
}

static int ru_get_u8(ru_reader_t *r, unsigned *value)
{
    if (r->length - r->offset < 1)
        return ru_fail(r, "file is truncated");
    *value = r->data[r->offset++];
    return 0;
}

static int ru_get_u32(ru_reader_t *r, uint32_t *value)
{
    int i;
    if (r->length - r->offset < 4)
        return ru_fail(r, "file is truncated");
    *value = 0;
    for (i = 0; i < 4; ++i)
        *value |= (uint32_t)r->data[r->offset + i] << (8 * i);
    r->offset += 4;
    return 0;
}

static int ru_get_u64(ru_reader_t *r, uint64_t *value)
{
    int i;
    if (r->length - r->offset < 8)
        return ru_fail(r, "file is truncated");
    *value = 0;
    for (i = 0; i < 8; ++i)
        *value |= (uint64_t)r->data[r->offset + i] << (8 * i);
    r->offset += 8;
    return 0;
}

/* A count of records, each at least min_size bytes, must fit. */
static int ru_get_count(ru_reader_t *r, size_t min_size, size_t *count)
{
    uint32_t value;
    if (ru_get_u32(r, &value) != 0)
        return -1;
    if (min_size != 0 && value > (r->length - r->offset) / min_size)
        return ru_fail(r, "a section count is larger than the file");
    *count = value;
    return 0;
}

static int ru_get_string(ru_reader_t *r, int nullable, char **text)
{
    uint32_t length;

    *text = NULL;
    if (ru_get_u32(r, &length) != 0)
        return -1;
    if (length == RU_NULL_STRING)
    {
        if (!nullable)
            return ru_fail(r, "a required string is missing");
        return 0;
    }
    if (length > r->length - r->offset)
        return ru_fail(r, "a string runs past the end of the file");
    if (memchr(r->data + r->offset, '\0', length) != NULL)
        return ru_fail(r, "a string contains a NUL byte");

    *text = malloc((size_t)length + 1);
    if (*text == NULL)
        return ru_fail(r, "out of memory");
    memcpy(*text, r->data + r->offset, length);
    (*text)[length] = '\0';
    r->offset += length;
    return 0;
}

static int ru_known_opcode(unsigned opcode)
{
    switch (opcode)
    {
        case USSR_BC_NOP:
        case USSR_BC_LOAD_CONST: case USSR_BC_LOAD_VAR:
        case USSR_BC_STORE_VAR: case USSR_BC_LOAD_HASH:
        case USSR_BC_STORE_HASH: case USSR_BC_DECODE_UNO:
        case USSR_BC_GET: case USSR_BC_VECTOR_GET: case USSR_BC_VECTOR_SET:
        case USSR_BC_ADD: case USSR_BC_SUB: case USSR_BC_MUL:
        case USSR_BC_DIV: case USSR_BC_MOD: case USSR_BC_NEG:
        case USSR_BC_EQ: case USSR_BC_NE: case USSR_BC_LT:
        case USSR_BC_LE: case USSR_BC_GT: case USSR_BC_GE:
        case USSR_BC_LAND: case USSR_BC_LOR: case USSR_BC_XOR:
        case USSR_BC_BAND: case USSR_BC_BOR: case USSR_BC_SHL:
        case USSR_BC_SHR:
        case USSR_BC_JMP: case USSR_BC_JMP_TRUE: case USSR_BC_JMP_FALSE:
        case USSR_BC_SET: case USSR_BC_PRINT: case USSR_BC_CONCAT:
        case USSR_BC_EXTERNAL: case USSR_BC_OOP: case USSR_BC_EVAL:
        case USSR_BC_RANDOM64: case USSR_BC_SEED64: case USSR_BC_SCAN:
        case USSR_BC_TIME: case USSR_BC_CHAIN: case USSR_BC_FILE:
        case USSR_BC_CD: case USSR_BC_TEMPLATE:
        case USSR_BC_CALL: case USSR_BC_RET: case USSR_BC_RETURN:
        case USSR_BC_HALT:
            return 1;
        default:
            return 0;
    }
}

static ussr_command_t *ru_get_site(ru_reader_t *r, uint32_t *instruction)
{
    ussr_command_t *command;
    unsigned count;
    unsigned i;

    command = calloc(1, sizeof(*command));
    if (command == NULL)
    {
        ru_fail(r, "out of memory");
        return NULL;
    }

    if (ru_get_u32(r, instruction) != 0 ||
        ru_get_string(r, 1, &command->name) != 0 ||
        ru_get_string(r, 1, &command->return_name) != 0 ||
        ru_get_u8(r, &count) != 0)
        goto fail;

    if (count > 0)
    {
        command->arguments = calloc(count, sizeof(*command->arguments));
        if (command->arguments == NULL)
        {
            ru_fail(r, "out of memory");
            goto fail;
        }
    }

    for (i = 0; i < count; ++i)
    {
        ussr_argument_t *argument = &command->arguments[i];
        unsigned kind;
        unsigned assignment;

        argument->type = USSR_ARGUMENT_VALUE;
        argument->data.value = ussr_null();
        command->argument_count = i + 1;

        if (ru_get_u8(r, &kind) != 0 || ru_get_u8(r, &assignment) != 0)
            goto fail;
        if (assignment > 1)
        {
            ru_fail(r, "invalid argument flag in a call site");
            goto fail;
        }
        argument->assignment = (int)assignment;

        if (kind == RU_ARG_VARIABLE)
        {
            ussr_expression_t *expression = calloc(1, sizeof(*expression));
            if (expression == NULL)
            {
                ru_fail(r, "out of memory");
                goto fail;
            }
            expression->type = USSR_EXPR_VARIABLE;
            argument->type = USSR_ARGUMENT_EXPRESSION;
            argument->data.expression = expression;
            if (ru_get_string(r, 0, &expression->data.variable) != 0)
                goto fail;
        }
        else if (kind != RU_ARG_VALUE)
        {
            ru_fail(r, "unknown argument kind in a call site");
            goto fail;
        }
    }

    return command;

fail:
    {
        ussr_command_list_t *holder = calloc(1, sizeof(*holder));
        if (holder != NULL)
        {
            holder->head = holder->tail = command;
            ussr_command_list_free(holder);
        }
    }
    return NULL;
}

static int ru_read_sites(ru_reader_t *r, const ussr_bc_program_t *program,
                         ussr_command_list_t *list, unsigned opcode,
                         ussr_bc_oop_site_t **sites, size_t *site_count)
{
    size_t count;
    size_t i;

    /* instruction + name + return name + argument count */
    if (ru_get_count(r, 13, &count) != 0)
        return -1;

    if (count > 0)
    {
        *sites = calloc(count, sizeof(**sites));
        if (*sites == NULL)
            return ru_fail(r, "out of memory");
    }

    for (i = 0; i < count; ++i)
    {
        uint32_t instruction;
        ussr_command_t *command = ru_get_site(r, &instruction);

        if (command == NULL)
            return -1;

        if (list->tail != NULL)
            list->tail->next = command;
        else
            list->head = command;
        list->tail = command;

        if (instruction >= program->code_count ||
            program->code[instruction].opcode != opcode)
            return ru_fail(r, "a call site does not point at its instruction");

        (*sites)[i].instruction = instruction;
        (*sites)[i].command = command;
        *site_count = i + 1;
    }

    return 0;
}

static int ru_parse(ru_reader_t *r, ussr_bc_program_t *p,
                    ussr_command_list_t *list)
{
    unsigned version;
    uint32_t abi;
    uint32_t producer;
    uint32_t payload;
    uint32_t stored_crc;
    size_t count;
    size_t i;
    size_t j;

    if (r->length < USSR_RU_MAGIC_SIZE ||
        memcmp(r->data, USSR_RU_MAGIC, USSR_RU_MAGIC_SIZE) != 0)
        return ru_fail(r, "not a USSR bytecode file (bad signature)");
    if (r->length < RU_HEADER_SIZE + 4)
        return ru_fail(r, "file is truncated");

    r->offset = USSR_RU_MAGIC_SIZE;
    version = (unsigned)r->data[8] | ((unsigned)r->data[9] << 8);
    if (version != USSR_RU_FORMAT_VERSION)
        return ru_fail(r, "unsupported .ru format version");
    if (r->data[10] != 0 || r->data[11] != 0)
        return ru_fail(r, "unsupported .ru flags");
    r->offset = 12;
    ru_get_u32(r, &abi);
    ru_get_u32(r, &producer);
    ru_get_u32(r, &payload);
    (void)producer;
    if (abi != USSR_RU_VM_ABI)
        return ru_fail(r, "compiled for an incompatible VM (ABI mismatch)");
    if (payload != r->length - RU_HEADER_SIZE - 4)
        return ru_fail(r, payload > r->length - RU_HEADER_SIZE - 4 ?
                       "file is truncated" :
                       "unexpected data after the end of the program");

    r->offset = r->length - 4;
    ru_get_u32(r, &stored_crc);
    if (stored_crc != ru_crc32(r->data, r->length - 4))
        return ru_fail(r, "checksum mismatch (file is corrupted)");

    /* Parse the payload only; the checksum is not part of any section. */
    r->length -= 4;
    r->offset = RU_HEADER_SIZE;

    if (ru_get_count(r, 8, &count) != 0)
        return -1;
    if (count == 0 || count > USSR_BC_MAX_CODE)
        return ru_fail(r, "invalid instruction count");
    p->code = calloc(count, sizeof(*p->code));
    if (p->code == NULL)
        return ru_fail(r, "out of memory");
    for (i = 0; i < count; ++i)
    {
        unsigned opcode, a, b, c;
        uint32_t immediate;
        ru_get_u8(r, &opcode);
        ru_get_u8(r, &a);
        ru_get_u8(r, &b);
        ru_get_u8(r, &c);
        ru_get_u32(r, &immediate);
        if (!ru_known_opcode(opcode))
            return ru_fail(r, "unknown opcode");
        p->code[i].opcode = (uint8_t)opcode;
        p->code[i].a = (uint8_t)a;
        p->code[i].b = (uint8_t)b;
        p->code[i].c = (uint8_t)c;
        p->code[i].immediate = immediate;
    }
    p->code_count = p->code_capacity = count;
    if (p->code[count - 1].opcode != USSR_BC_HALT)
        return ru_fail(r, "program does not end with HALT");

    if (ru_get_count(r, 1, &count) != 0)
        return -1;
    if (count > 0)
    {
        p->constants = calloc(count, sizeof(*p->constants));
        if (p->constants == NULL)
            return ru_fail(r, "out of memory");
    }
    for (i = 0; i < count; ++i)
    {
        unsigned tag;
        if (ru_get_u8(r, &tag) != 0)
            return -1;
        switch (tag)
        {
            case RU_CONST_NULL:
                p->constants[i] = ussr_null();
                break;
            case RU_CONST_INTEGER:
            {
                uint64_t bits;
                int64_t value;
                if (ru_get_u64(r, &bits) != 0)
                    return -1;
                value = (int64_t)bits;
                p->constants[i] = ussr_integer(value);
                break;
            }
            case RU_CONST_REAL:
            {
                uint64_t bits;
                double real;
                if (ru_get_u64(r, &bits) != 0)
                    return -1;
                memcpy(&real, &bits, sizeof(real));
                p->constants[i] = ussr_real(real);
                break;
            }
            case RU_CONST_STRING:
            {
                char *text;
                if (ru_get_string(r, 0, &text) != 0)
                    return -1;
                p->constants[i] = ussr_string(text);
                free(text);
                break;
            }
            case RU_CONST_BOOLEAN:
            {
                unsigned flag;
                if (ru_get_u8(r, &flag) != 0)
                    return -1;
                if (flag > 1)
                    return ru_fail(r, "invalid boolean constant");
                p->constants[i] = ussr_boolean((int)flag);
                break;
            }
            default:
                return ru_fail(r, "unknown constant type");
        }
        p->constant_count = i + 1;
    }
    p->constant_capacity = p->constant_count;

    if (ru_get_count(r, 4, &count) != 0)
        return -1;
    if (count > 0)
    {
        p->strings = calloc(count, sizeof(*p->strings));
        if (p->strings == NULL)
            return ru_fail(r, "out of memory");
    }
    for (i = 0; i < count; ++i)
    {
        p->string_count = i + 1;
        if (ru_get_string(r, 1, &p->strings[i]) != 0)
            return -1;
    }
    p->string_capacity = p->string_count;

    /* name + return name + entry + parameter count */
    if (ru_get_count(r, 16, &count) != 0)
        return -1;
    if (count > 0)
    {
        p->functions = calloc(count, sizeof(*p->functions));
        if (p->functions == NULL)
            return ru_fail(r, "out of memory");
    }
    for (i = 0; i < count; ++i)
    {
        ussr_bc_function_t *f = &p->functions[i];
        size_t parameters;

        p->function_count = i + 1;
        if (ru_get_string(r, 0, &f->name) != 0 ||
            ru_get_string(r, 1, &f->return_name) != 0 ||
            ru_get_u32(r, &f->entry) != 0 ||
            ru_get_count(r, 4, &parameters) != 0)
            return -1;
        if (f->entry >= p->code_count)
            return ru_fail(r, "a function entry is outside the program");
        if (parameters > USSR_BC_MAX_REGS)
            return ru_fail(r, "a function has too many parameters");
        if (parameters > 0)
        {
            f->parameters = calloc(parameters, sizeof(*f->parameters));
            if (f->parameters == NULL)
                return ru_fail(r, "out of memory");
        }
        for (j = 0; j < parameters; ++j)
        {
            f->parameter_count = j + 1;
            if (ru_get_string(r, 0, &f->parameters[j]) != 0)
                return -1;
        }

        /* Local names: at least the parameters and the return variable. */
        {
            size_t locals;

            if (ru_get_count(r, 4, &locals) != 0)
                return -1;
            if (locals < parameters + 1)
                return ru_fail(r, "a function lists too few local names");
            f->locals = calloc(locals, sizeof(*f->locals));
            if (f->locals == NULL)
                return ru_fail(r, "out of memory");
            for (j = 0; j < locals; ++j)
            {
                f->local_count = j + 1;
                if (ru_get_string(r, 0, &f->locals[j]) != 0)
                    return -1;
            }
        }
    }
    p->function_capacity = p->function_count;

    /* Operands that index a pool must stay inside it. */
    for (i = 0; i < p->code_count; ++i)
    {
        const ussr_bc_instruction_t *ins = &p->code[i];
        switch (ins->opcode)
        {
            case USSR_BC_JMP: case USSR_BC_JMP_TRUE: case USSR_BC_JMP_FALSE:
                if (ins->immediate >= p->code_count)
                    return ru_fail(r, "a jump leaves the program");
                break;
            case USSR_BC_LOAD_CONST:
                if (ins->immediate >= p->constant_count)
                    return ru_fail(r, "a constant index is out of range");
                break;
            case USSR_BC_CALL:
                if (ins->immediate >= p->function_count)
                    return ru_fail(r, "a function index is out of range");
                break;
            case USSR_BC_LOAD_VAR: case USSR_BC_STORE_VAR:
                if (ins->b > USSR_VAR_GLOBAL)
                    return ru_fail(r, "a variable has an unknown scope");
                /* fall through */
            case USSR_BC_LOAD_HASH: case USSR_BC_STORE_HASH:
            case USSR_BC_DECODE_UNO: case USSR_BC_OOP:
            case USSR_BC_SCAN: case USSR_BC_EXTERNAL:
                if (ins->immediate >= p->string_count ||
                    p->strings[ins->immediate] == NULL)
                    return ru_fail(r, "a string index is out of range");
                break;
            default:
                break;
        }
    }

    if (ru_read_sites(r, p, list, USSR_BC_OOP,
                      &p->oop_sites, &p->oop_site_count) != 0)
        return -1;
    p->oop_site_capacity = p->oop_site_count;

    {
        ussr_bc_oop_site_t *scan = NULL;
        size_t scan_count = 0;
        int status = ru_read_sites(r, p, list, USSR_BC_SCAN,
                                   &scan, &scan_count);

        if (scan_count > 0)
        {
            p->scan_sites = calloc(scan_count, sizeof(*p->scan_sites));
            if (p->scan_sites == NULL)
                status = ru_fail(r, "out of memory");
        }
        for (i = 0; p->scan_sites != NULL && i < scan_count; ++i)
        {
            p->scan_sites[i].instruction = scan[i].instruction;
            p->scan_sites[i].command = scan[i].command;
            p->scan_site_count = i + 1;
        }
        free(scan);
        if (status != 0)
            return -1;
        p->scan_site_capacity = p->scan_site_count;
    }

    if (r->offset != r->length)
        return ru_fail(r, "unexpected data after the last section");

    return 0;
}

int ussr_ru_read_file(
    const char *path,
    ussr_bc_program_t *program,
    ussr_command_list_t **site_commands)
{
    FILE *input;
    unsigned char *data;
    long size;
    ru_reader_t r;
    ussr_command_list_t *list;

    memset(program, 0, sizeof(*program));
    *site_commands = NULL;

    input = fopen(path, "rb");
    if (input == NULL)
    {
        fprintf(stderr, "USSR: cannot open '%s'\n", path);
        return -1;
    }
    if (fseek(input, 0, SEEK_END) != 0 || (size = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "USSR: cannot read '%s'\n", path);
        fclose(input);
        return -1;
    }
    if ((unsigned long)size > RU_MAX_FILE_SIZE)
    {
        fprintf(stderr, "USSR: '%s': file is too large\n", path);
        fclose(input);
        return -1;
    }

    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL)
    {
        fclose(input);
        return -1;
    }
    if (fread(data, 1, (size_t)size, input) != (size_t)size)
    {
        fprintf(stderr, "USSR: short read from '%s'\n", path);
        free(data);
        fclose(input);
        return -1;
    }
    fclose(input);

    list = calloc(1, sizeof(*list));
    if (list == NULL)
    {
        free(data);
        return -1;
    }

    memset(&r, 0, sizeof(r));
    r.data = data;
    r.length = (size_t)size;
    r.path = path;

    if (ru_parse(&r, program, list) != 0)
    {
        fprintf(stderr, "USSR: '%s': %s\n", path,
                r.error != NULL ? r.error : "invalid bytecode file");
        ussr_bc_program_free(program);
        ussr_command_list_free(list);
        free(data);
        return -1;
    }

    free(data);
    *site_commands = list;
    return 0;
}
