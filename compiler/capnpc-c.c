/* capnpc-c.c
 *
 * Copyright (C) 2013 James McKaskill
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * Modified by Raysen Microsystem Technology Co., Ltd. 2025-2026.
 * RT-Thread/MISRA C:2012 adaptation for RS500 platform.
 */
#ifndef _MSC_VER
#define _POSIX_C_SOURCE 200809L
#endif

#include "schema.capnp.h"
#include "str.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows specific headers for binary mode */
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
/* Define POSIX file descriptor constants for Windows */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#endif

/* strings.h is POSIX-only, not available on Windows */
#ifndef _MSC_VER
#include <strings.h>
#endif

/* ---------------- Naming Utilities ---------------- */
typedef enum
{
    NAME_STRUCT,
    NAME_FIELD,
    NAME_FUNC,
    NAME_ENUM_TYPE,
    NAME_ENUM_VALUE
} NameType;

/* CamelCase -> snake_case, compatible with json_to_c_struct.py naming */
static char *to_snake_case(const char *input)
{
    if (input == NULL)
    {
        return strdup("");
    }
    size_t len = strlen(input);
    char *output = malloc(len * 3 + 1); /* Extra space for digit handling */
    if (output == NULL)
    {
        fprintf(stderr, "Error: malloc failed in to_snake_case (output buffer)\n");
        exit(EXIT_FAILURE);
    }
    size_t j = 0;

    /* Step 1: Standard CamelCase to snake_case conversion */
    for (size_t i = 0; i < len; ++i)
    {
        char c = input[i];
        char next = input[i + 1];
        if (isupper((unsigned char)c))
        {
            if (i > 0 && !isupper((unsigned char)input[i - 1]))
            {
                output[j++] = '_';
            }
            else if ((next != 0) && (0 != islower((unsigned char)next)))
            {
                if (i > 0)
                    output[j++] = '_';
            }
            output[j++] = (char)tolower((unsigned char)c);
        }
        else
        {
            output[j++] = c;
        }
    }
    output[j] = '\0';

    /* Step 2: Handle leading digits (e.g., "422YuvOrder" -> "yuv422_order") */
    /* Match json_to_c_struct.py rule: move digits after first word */
    if (j > 0 && isdigit((unsigned char)output[0]))
    {
        /* Extract leading digits */
        size_t digit_end = 0;
        while (digit_end < j && isdigit((unsigned char)output[digit_end]))
        {
            digit_end++;
        }

        /* Skip underscores after digits */
        size_t word_start = digit_end;
        while (word_start < j && output[word_start] == '_')
        {
            word_start++;
        }

        if (word_start < j)
        {
            /* Find the end of the first word after digits */
            size_t first_word_end = word_start;
            while (first_word_end < j && output[first_word_end] != '_')
            {
                first_word_end++;
            }

            /* Build new string: first_word + digits + rest */
            char *temp = malloc(len * 3 + 1);
            size_t k = 0;

            /* Copy first word */
            for (size_t i = word_start; i < first_word_end; i++)
            {
                temp[k++] = output[i];
            }

            /* Append digits (without underscore) */
            for (size_t i = 0; i < digit_end; i++)
            {
                temp[k++] = output[i];
            }

            /* Copy rest of string (including leading underscore if present) */
            for (size_t i = first_word_end; i < j; i++)
            {
                temp[k++] = output[i];
            }

            temp[k] = '\0';
            free(output);
            output = temp;
            j = k;
        }
    }

    /* Step 3: Remove duplicate word prefix (e.g., "irsc_irsc_info" -> "irsc_info") */
    /* Do not add underscore before digits */
    char *final_output = malloc(len * 3 + 1);
    if (final_output == NULL)
    {
        fprintf(stderr, "Error: malloc failed in to_snake_case (final_output buffer)\n");
        free(output);
        exit(EXIT_FAILURE);
    }
    size_t out_idx = 0;

    /* Detect duplicate prefix: find first underscore, check if repeated */
    size_t first_underscore = 0;
    while (first_underscore < j && output[first_underscore] != '_')
    {
        first_underscore++;
    }

    /* If underscore found and not at the last position */
    if (first_underscore > 0 && first_underscore < j - 1)
    {
        /* Check if first word matches beginning of second word */
        /* e.g.: "irsc_irsc_info" -> check "irsc" matches prefix of "irsc_info" */
        size_t second_word_start = first_underscore + 1;

        /* Check for exact duplication (e.g., "irsc_irsc") */
        bool is_duplicate = true;
        for (size_t i = 0; i < first_underscore; i++)
        {
            if (second_word_start + i >= j || output[i] != output[second_word_start + i])
            {
                is_duplicate = false;
                break;
            }
        }

        /* If duplicate, also check next char is underscore or end */
        if (is_duplicate && second_word_start + first_underscore < j)
        {
            if (output[second_word_start + first_underscore] != '_')
            {
                is_duplicate = false;
            }
        }

        if (is_duplicate)
        {
            /* Skip first duplicated word and underscore */
            for (size_t i = second_word_start; i < j; i++)
            {
                final_output[out_idx++] = output[i];
            }
        }
        else
        {
            /* No duplication, copy directly */
            for (size_t i = 0; i < j; i++)
            {
                final_output[out_idx++] = output[i];
            }
        }
    }
    else
    {
        /* No underscore or too short, copy directly */
        for (size_t i = 0; i < j; i++)
        {
            final_output[out_idx++] = output[i];
        }
    }

    final_output[out_idx] = '\0';

    free(output);

    /* Step 4: Convert "_to" followed by digit pattern to "_" */
    /* e.g.: "gap_col48_to55" -> "gap_col48_55" */
    /* Note: CamelCase "gapCol48To55" becomes "gap_col48_to55" in snake_case */
    /* This restores original CSV field name format (Cap'n Proto disallows underscores) */
    char *result = malloc(strlen(final_output) + 1);
    if (result == NULL)
    {
        fprintf(stderr, "Error: malloc failed in to_snake_case (result buffer)\n");
        free(final_output);
        exit(EXIT_FAILURE);
    }

    size_t src_idx = 0;
    size_t dst_idx = 0;
    size_t final_len = strlen(final_output);

    while (src_idx < final_len)
    {
        /* Check for "_to" followed by digit pattern */
        /* Pattern: digit + "_to" + digit (e.g., "48_to55") */
        if (src_idx > 0 && isdigit((unsigned char)final_output[src_idx - 1]) && src_idx + 3 <= final_len &&
            final_output[src_idx] == '_' && final_output[src_idx + 1] == 't' && final_output[src_idx + 2] == 'o' &&
            src_idx + 3 < final_len && isdigit((unsigned char)final_output[src_idx + 3]))
        {
            /* Replace "_to" with "_" */
            result[dst_idx++] = '_';
            src_idx += 3; /* Skip "_to" */
        }
        else
        {
            result[dst_idx++] = final_output[src_idx++];
        }
    }
    result[dst_idx] = '\0';

    free(final_output);
    return result;
}

/* Convert all characters to uppercase */
static char *to_upper_case(const char *input)
{
    if (input == NULL)
    {
        return strdup("");
    }
    size_t len = strlen(input);
    char *output = malloc(len + 1);
    if (output == NULL)
    {
        fprintf(stderr, "Error: malloc failed in to_upper_case\n");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < len; ++i)
    {
        output[i] = (char)toupper((unsigned char)input[i]);
    }
    output[len] = '\0';
    return output;
}

/**
 * @brief Generate header guard macro from filename
 *
 * Converts a filename like "default_cfg.capnp" to "DEFAULT_CFG_CAPNP_H"
 * This replaces the random CAPN_XXXX style guard with a meaningful name.
 *
 * @param filename The source filename (e.g., "default_cfg.capnp")
 * @return Allocated string with the guard macro name (caller must free)
 */
static char *generate_header_guard(const char *filename)
{
    if (filename == NULL)
    {
        return strdup("CAPNP_GENERATED_H");
    }

    /* Find the basename (after last '/') */
    const char *basename = strrchr(filename, '/');
    basename = basename ? basename + 1 : filename;

    /* Allocate buffer for the guard macro */
    size_t len = strlen(basename);
    char *guard = malloc(len + 8); /* Extra space for "_H" and safety */
    if (guard == NULL)
    {
        fprintf(stderr, "Error: malloc failed in generate_header_guard\n");
        exit(EXIT_FAILURE);
    }

    /* Convert to uppercase and replace '.' with '_' */
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = basename[i];
        if (c == '.')
        {
            guard[j++] = '_';
        }
        else if (isalnum((unsigned char)c))
        {
            guard[j++] = (char)toupper((unsigned char)c);
        }
        else
        {
            guard[j++] = '_';
        }
    }

    /* Append "_H" suffix */
    guard[j++] = '_';
    guard[j++] = 'H';
    guard[j] = '\0';

    return guard;
}

/* Collapse consecutive underscores */
static void collapse_underscores(char *s)
{
    if (s == NULL)
    {
        return;
    }
    char *w = s;
    int32_t prev_us = 0;
    for (char *r = s; *r != '\0'; ++r)
    {
        if (*r == '_')
        {
            if (prev_us == 0)
            {
                *w++ = *r;
            }
            prev_us = 1;
        }
        else
        {
            *w++ = *r;
            prev_us = 0;
        }
    }
    *w = '\0';
}

/* Core interface: return normalized name by type */
static char *normalize_name(const char *orig, const char *module, NameType type)
{
    if (orig == NULL)
    {
        orig = "";
    }
    if (module == NULL)
    {
        module = "";
    }

    char *snake = to_snake_case(orig);
    collapse_underscores(snake);
    char *result = NULL;

    switch (type)
    {
    case NAME_STRUCT:
    case NAME_ENUM_TYPE:
    {
        /* Check and remove duplicate parent name prefix */
        const char *final_snake = snake;
        const char *module_snake = module; /* Use module as parent name, usually already snake_case */
        size_t module_len = strlen(module_snake);

        /* Check if struct/enum name starts with parent name followed by '_' */
        /* 如果满足，则跳过父级名称以避免重复 */
        if (module_len > 0 && strncmp(module_snake, snake, module_len) == 0 && snake[module_len] == '_')
        {
            final_snake = snake + module_len + 1;
        }

        /* Allocate memory */
        /* Note: capn_ prefix added by caller in format string */
        size_t buflen = strlen(module) + strlen(final_snake) + 10;
        result = malloc(buflen);
        if (result == NULL)
        {
            fprintf(stderr, "Error: malloc failed in normalize_name (NAME_STRUCT/NAME_ENUM_TYPE)\n");
            free(snake);
            exit(EXIT_FAILURE);
        }

        /* Concat: module_struct_t if module non-empty, else struct_t */
        if (module_len > 0)
        {
            snprintf(result, buflen, "%s_%s_t", module, final_snake);
        }
        else
        {
            snprintf(result, buflen, "%s_t", final_snake);
        }
        /* End of dedup logic */
        break;
    }

    case NAME_FIELD:
    case NAME_FUNC:
    {
        /* Check and remove duplicate parent name prefix */
        const char *final_snake = snake;
        const char *module_snake = module;
        size_t module_len = strlen(module_snake);
        bool already_has_prefix = false;

        /* Check if field/func name starts with parent name followed by '_' */
        /* e.g.: module="irsc", snake="irsc_irsc_info" */
        /* -> 检测到重复，跳过第一个 "irsc_"，得到 "irsc_info" */
        /* -> 最终不再添加前缀，直接使用 "irsc_info" */
        if (module_len > 0 && strncmp(module_snake, snake, module_len) == 0 && snake[module_len] == '_')
        {
            /* Field already has module prefix, skip duplicate */
            final_snake = snake + module_len + 1; /* Skip "module_" part */
            already_has_prefix = true; /* Mark prefix already present */
        }

        /* Allocate memory (capn_ prefix added by caller) */
        size_t buflen = strlen(module) + strlen(final_snake) + 10;
        result = malloc(buflen);
        if (result == NULL)
        {
            fprintf(stderr, "Error: malloc failed in normalize_name (NAME_FIELD/NAME_FUNC)\n");
            free(snake);
            exit(EXIT_FAILURE);
        }

        /* Use deduped name if prefix detected, else add module prefix */
        if (already_has_prefix)
        {
            /* Already has prefix, use final_snake directly */
            snprintf(result, buflen, "%s", final_snake);
        }
        else if (module_len > 0)
        {
            /* No prefix, need to add (e.g., "ctrl_config" -> "irsc_ctrl_config") */
            snprintf(result, buflen, "%s_%s", module, final_snake);
        }
        else
        {
            snprintf(result, buflen, "%s", final_snake);
        }
        /* End of dedup logic */
        break;
    }

    case NAME_ENUM_VALUE:
    {
        char *upper = to_upper_case(snake);
        char *mod_upper = to_upper_case(module);

        /* Check and remove duplicate module name */
        const char *final_upper = upper;
        size_t mod_upper_len = strlen(mod_upper);

        /* Check if enum value starts with module name followed by '_' */
        if (mod_upper_len > 0 && strncmp(mod_upper, upper, mod_upper_len) == 0 && upper[mod_upper_len] == '_')
        {
            final_upper = upper + mod_upper_len + 1;
        }

        size_t buflen = strlen(mod_upper) + strlen(final_upper) + 10;
        result = malloc(buflen);
        if (result == NULL)
        {
            fprintf(stderr, "Error: malloc failed in normalize_name (NAME_ENUM_VALUE)\n");
            free(mod_upper);
            free(upper);
            free(snake);
            exit(EXIT_FAILURE);
        }
        snprintf(result, buflen, "%s_%s", mod_upper, final_upper);
        free(mod_upper);
        free(upper);
        break;
    }

    default:
        break;
    }

    free(snake);
    return result;
}

static const char *g_module_name = NULL;

struct value
{
    struct Type t;
    const char *tname;
    struct str tname_buf;
    struct Value v;
    capn_ptr ptrval;
    int64_t intval;
};

struct field
{
    struct Field f;
    struct value v;
    struct node *group;
};

struct node
{
    struct capn_tree hdr;
    struct Node n;
    struct node *next;
    struct node *file_nodes, *next_file_node;
    struct str name;
    struct field *fields;
};

struct id_bst
{
    uint64_t id;
    struct id_bst *left;
    struct id_bst *right;
};

static struct str SRC = STR_INIT, HDR = STR_INIT;
static struct capn g_valcapn;
static struct capn_segment g_valseg;
static int32_t g_valc;
static int32_t g_val0used, g_nullused;

static int32_t g_fieldgetset = 0;

static struct capn_tree *g_node_tree;

static struct node *find_node_mayfail(uint64_t id)
{
    struct node *s = (struct node *)g_node_tree;
    while ((s != NULL) && (s->n.id != id))
    {
        s = (struct node *)s->hdr.link[s->n.id < id];
    }
    return s;
}

static struct node *find_node(uint64_t id)
{
    struct node *s = find_node_mayfail(id);
    if (s == NULL)
    {
        fprintf(stderr, "cant find node with id 0x%x%x\n", (uint32_t)(id >> 32), (uint32_t)id);
        exit(2);
    }
    return s;
}

static void insert_node(struct node *s)
{
    struct capn_tree **x = &g_node_tree;
    while (*x)
    {
        s->hdr.parent = *x;
        x = &(*x)->link[((struct node *)*x)->n.id < s->n.id];
    }
    *x = &s->hdr;
    g_node_tree = capn_tree_insert(g_node_tree, &s->hdr);
}

static struct id_bst *insert_id(struct id_bst *bst, uint64_t id)
{
    struct id_bst **current = &bst;

    while (*current)
    {
        if (id > (*current)->id)
        {
            current = &(*current)->right;
        }
        else if (id < (*current)->id)
        {
            current = &(*current)->left;
        }
        else
        {
            return bst;
        }
    }

    *current = malloc(sizeof **current);
    if (!(*current))
    {
        fprintf(stderr, "Error: malloc failed in insert_id\n");
        exit(EXIT_FAILURE);
    }
    (*current)->id = id;
    (*current)->left = NULL;
    (*current)->right = NULL;

    return bst;
}

static bool contains_id(struct id_bst *bst, uint64_t id)
{
    struct id_bst *current = bst;

    while (current)
    {
        if (id == current->id)
        {
            return true;
        }
        else if (id < current->id)
        {
            current = current->left;
        }
        else
        {
            current = current->right;
        }
    }

    return false;
}

static void free_id_bst(struct id_bst *bst)
{
    /* Iterative post-order traversal using explicit stack (MISRA: no recursion) */
    struct id_bst **stack = NULL;
    size_t stack_size = 0;
    size_t stack_cap = 0;

    if (bst == NULL)
    {
        return;
    }

    /* Push root */
    stack_cap = 64;
    stack = malloc(stack_cap * sizeof(*stack));
    if (stack == NULL)
    {
        return;
    }
    stack[stack_size++] = bst;

    while (stack_size > 0)
    {
        struct id_bst *current = stack[stack_size - 1];
        stack_size--;

        /* Push children before freeing parent (will be freed later) */
        if (current->left != NULL)
        {
            if (stack_size >= stack_cap)
            {
                stack_cap *= 2;
                struct id_bst **new_stack = realloc(stack, stack_cap * sizeof(*stack));
                if (new_stack == NULL)
                {
                    free(stack);
                    return;
                }
                stack = new_stack;
            }
            stack[stack_size++] = current->left;
        }
        if (current->right != NULL)
        {
            if (stack_size >= stack_cap)
            {
                stack_cap *= 2;
                struct id_bst **new_stack = realloc(stack, stack_cap * sizeof(*stack));
                if (new_stack == NULL)
                {
                    free(stack);
                    return;
                }
                stack = new_stack;
            }
            stack[stack_size++] = current->right;
        }
        free(current);
    }

    free(stack);
}

/* resolve_names iteratively follows the nestedNodes tree in order to
 * set node->name.
 * It also builds up the list of nodes within a file (file_nodes and
 * next_file_node).
 * MISRA C:2012 Rule 17.2: No recursion - uses explicit stack. */

struct resolve_frame
{
    struct node *n;
    capn_text name;
    int32_t saved_len;
    int32_t phase;    /* 0=init, 1=nested iteration, 2=group iteration, 3=done */
    int32_t iter_idx; /* current iteration index */
};

static void resolve_names(struct str *b, struct node *root_n, capn_text root_name,
                          struct node *file, const char *namespace)
{
    static struct str converted_name = STR_INIT;

    size_t stack_cap = 64;
    size_t stack_size = 0;
    struct resolve_frame *stack = malloc(stack_cap * sizeof(*stack));
    if (stack == NULL)
    {
        return;
    }

    /* Push initial frame */
    stack[0].n = root_n;
    stack[0].name = root_name;
    stack[0].saved_len = b->len;
    stack[0].phase = 0;
    stack[0].iter_idx = 0;
    stack_size = 1;

    while (stack_size > 0)
    {
        struct resolve_frame *fr = &stack[stack_size - 1];

        if (fr->phase == 0)
        {
            /* Phase 0: Initialize - add namespace and name to b */
            fr->saved_len = b->len;
            str_add(b, namespace, -1);

            if (fr->name.str == NULL)
            {
                str_add(&fr->n->name, b->str, b->len);
                /* Skip to done, restore b */
                str_setlen(b, fr->saved_len);
                stack_size--;
                continue;
            }

            str_reset(&converted_name);
            for (int j = 0; j < fr->name.len; j++)
            {
                char c = fr->name.str[j];
                if ((j > 0) && (c >= 'A') && (c <= 'Z'))
                {
                    str_add(&converted_name, "_", 1);
                }
                if ((c >= 'A') && (c <= 'Z'))
                {
                    c = (char)(c - 'A' + 'a');
                }
                str_add(&converted_name, &c, 1);
            }

            str_add(b, converted_name.str, converted_name.len);
            str_add(&fr->n->name, b->str, b->len);
            str_add(b, "_", 1);

            fr->phase = 1;
            fr->iter_idx = capn_len(fr->n->n.nestedNodes) - 1;
        }

        if (fr->phase == 1)
        {
            /* Phase 1: Iterate nested nodes */
            if (fr->iter_idx >= 0)
            {
                struct Node_NestedNode nest;
                get_Node_NestedNode(&nest, fr->n->n.nestedNodes, fr->iter_idx);
                struct node *nn = find_node(nest.id);
                fr->iter_idx--;

                if (nn != NULL)
                {
                    /* Push child frame */
                    if (stack_size >= stack_cap)
                    {
                        stack_cap *= 2;
                        struct resolve_frame *new_stack = realloc(stack, stack_cap * sizeof(*stack));
                        if (new_stack == NULL)
                        {
                            free(stack);
                            return;
                        }
                        stack = new_stack;
                        fr = &stack[stack_size - 1];
                    }
                    stack[stack_size].n = nn;
                    stack[stack_size].name = nest.name;
                    stack[stack_size].saved_len = 0;
                    stack[stack_size].phase = 0;
                    stack[stack_size].iter_idx = 0;
                    stack_size++;
                }
                continue;
            }

            /* Move to phase 2: group fields */
            fr->phase = 2;
            if (fr->n->n.which == Node__struct)
            {
                fr->iter_idx = capn_len(fr->n->n._struct.fields) - 1;
            }
            else
            {
                fr->iter_idx = -1;
            }
        }

        if (fr->phase == 2)
        {
            /* Phase 2: Iterate group fields */
            if (fr->iter_idx >= 0)
            {
                int idx = fr->iter_idx;
                fr->iter_idx--;

                if (fr->n->fields[idx].group != NULL)
                {
                    /* Push child frame */
                    if (stack_size >= stack_cap)
                    {
                        stack_cap *= 2;
                        struct resolve_frame *new_stack = realloc(stack, stack_cap * sizeof(*stack));
                        if (new_stack == NULL)
                        {
                            free(stack);
                            return;
                        }
                        stack = new_stack;
                        fr = &stack[stack_size - 1];
                    }
                    stack[stack_size].n = fr->n->fields[idx].group;
                    stack[stack_size].name = fr->n->fields[idx].f.name;
                    stack[stack_size].saved_len = 0;
                    stack[stack_size].phase = 0;
                    stack[stack_size].iter_idx = 0;
                    stack_size++;
                }
                continue;
            }

            fr->phase = 3;
        }

        if (fr->phase == 3)
        {
            /* Phase 3: Done - register file node and restore b */
            if ((fr->n->n.which != Node__struct) || (fr->n->n._struct.isGroup == 0))
            {
                fr->n->next_file_node = file->file_nodes;
                file->file_nodes = fr->n;
            }

            str_setlen(b, fr->saved_len);
            stack_size--;
        }
    }

    free(stack);
}

static void define_enum(struct node *n)
{
    int32_t i;

    char *enum_type = normalize_name(n->name.str, g_module_name, NAME_ENUM_TYPE);

    str_addf(&HDR, "\ntypedef enum %s {", enum_type);
    for (i = 0; i < capn_len(n->n._enum.enumerants); i++)
    {
        struct Enumerant e;
        get_Enumerant(&e, n->n._enum.enumerants, i);
        if (i != 0)
        {
            str_addf(&HDR, ",");
        }
        char *enum_val = normalize_name(e.name.str, NULL, NAME_ENUM_VALUE);
        char *full_enum_val = normalize_name(n->name.str, g_module_name, NAME_ENUM_VALUE);
        str_addf(&HDR, "\n\t%s_%s = %d", full_enum_val, enum_val, i);
        free(enum_val);
        free(full_enum_val);
    }
    str_addf(&HDR, "\n} %s;\n", enum_type);

    for (i = capn_len(n->n.annotations) - 1; i >= 0; i--)
    {
        struct Annotation a;
        struct Value v;
        get_Annotation(&a, n->n.annotations, i);
        read_Value(&v, a.value);

        switch (a.id)
        {
        case 0xcefaf27713042144UL:
            if (v.which != Value_text)
            {
                fprintf(stderr, "schema breakage on $C::typedefto annotation\n");
                exit(2);
            }

            str_addf(&HDR, "\ntypedef %s %s;\n", enum_type, v.text.str);
            break;

        default:
            break;
        }
    }

    free(enum_type);
}

static void decode_value(struct value *v, Type_ptr type, Value_ptr value, const char *symbol)
{
    struct Type list_type;

    /* Add safety check */
    if (v == NULL)
    {
        fprintf(stderr, "Error: NULL value parameter passed to decode_value\n");
        return;
    }

    memset(v, 0, sizeof(*v));
    read_Type(&v->t, type);
    read_Value(&v->v, value);

    switch (v->t.which)
    {
    case Type__void:
        v->tname = "void";
        break;
    case Type__bool:
        v->tname = "rt_uint8_t";
        break;
    case Type_int8:
        v->tname = "rt_int8_t";
        break;
    case Type_int16:
        v->tname = "rt_int16_t";
        break;
    case Type_int32:
        v->tname = "rt_int32_t";
        break;
    case Type_int64:
        v->tname = "rt_int64_t";
        break;
    case Type_uint8:
        v->tname = "rt_uint8_t";
        break;
    case Type_uint16:
        v->tname = "rt_uint16_t";
        break;
    case Type_uint32:
        v->tname = "rt_uint32_t";
        break;
    case Type_uint64:
        v->tname = "rt_uint64_t";
        break;
    case Type_float32:
        v->tname = "float";
        break;
    case Type_float64:
        v->tname = "double";
        break;
    case Type_text:
        v->tname = "capn_text";
        break;
    case Type_data:
        v->tname = "capn_data";
        break;
    case Type__enum:
    {
        if (v->t._enum.typeId == 0)
        {
            fprintf(stderr, "Warning: enum typeId is 0 (unset)\n");
            v->tname = "int";
            break;
        }
        struct node *en = find_node(v->t._enum.typeId);
        if (en == NULL)
        {
            fprintf(stderr, "Warning: enum node not found for typeId 0x%lx%lx\n",
                    (unsigned long)(v->t._enum.typeId >> 32), (unsigned long)v->t._enum.typeId);
            v->tname = "int";
            break;
        }
        char *et = normalize_name(en->name.str, g_module_name, NAME_ENUM_TYPE);
        v->tname = strf(&v->tname_buf, "%s", et);
        free(et);
        break;
    }
    case Type__struct:
    case Type__interface:
    {
        if (v->t._struct.typeId == 0)
        {
            fprintf(stderr, "Warning: struct typeId is 0 (unset)\n");
            v->tname = "capn_ptr";
            break;
        }
        struct node *sn = find_node(v->t._struct.typeId);
        if (sn == NULL)
        {
            fprintf(stderr, "Warning: struct node not found for typeId 0x%lx%lx\n",
                    (unsigned long)(v->t._struct.typeId >> 32), (unsigned long)v->t._struct.typeId);
            v->tname = "capn_ptr";
            break;
        }
        char *base = normalize_name(sn->name.str, "", NAME_FUNC);
        v->tname = strf(&v->tname_buf, "capn_%s_ptr", base);
        free(base);
        break;
    }
    case Type_anyPointer:
        v->tname = "capn_ptr";
        break;
    case Type__list:
        read_Type(&list_type, v->t._list.elementType);

        switch (list_type.which)
        {
        case Type__void:
            v->tname = "capn_ptr";
            break;
        case Type__bool:
            v->tname = "capn_list1";
            break;
        case Type_int8:
        case Type_uint8:
            v->tname = "capn_list8";
            break;
        case Type_int16:
        case Type_uint16:
        case Type__enum:
            v->tname = "capn_list16";
            break;
        case Type_int32:
        case Type_uint32:
        case Type_float32:
            v->tname = "capn_list32";
            break;
        case Type_int64:
        case Type_uint64:
        case Type_float64:
            v->tname = "capn_list64";
            break;
        case Type_text:
        case Type_data:
        case Type_anyPointer:
        case Type__list:
            v->tname = "capn_ptr";
            break;
        case Type__struct:
        case Type__interface:
        {
            if (list_type._struct.typeId == 0)
            {
                fprintf(stderr, "Warning: list element struct typeId is 0 (unset)\n");
                v->tname = "capn_ptr";
                break;
            }
            struct node *ln = find_node(list_type._struct.typeId);
            if (ln == NULL)
            {
                fprintf(stderr, "Warning: list element struct node not found for typeId 0x%lx%lx\n",
                        (unsigned long)(list_type._struct.typeId >> 32), (unsigned long)list_type._struct.typeId);
                v->tname = "capn_ptr";
                break;
            }
            char *base = normalize_name(ln->name.str, "", NAME_FUNC);
            v->tname = strf(&v->tname_buf, "capn_%s_list", base);
            free(base);
            break;
        }
        default:
            break;
        }
    default:
        break;
    }

    switch (v->v.which)
    {
    case Value__bool:
        v->intval = v->v._bool;
        break;
    case Value_int8:
    case Value_uint8:
        v->intval = v->v.int8;
        break;
    case Value_int16:
    case Value_uint16:
        v->intval = v->v.int16;
        break;
    case Value__enum:
        v->intval = v->v._enum;
        break;
    case Value_int32:
    case Value_uint32:
    case Value_float32:
        v->intval = v->v.int32;
        break;
    case Value_int64:
    case Value_float64:
    case Value_uint64:
        v->intval = v->v.int64;
        break;
    case Value_text:
        if (v->v.text.len != 0)
        {
            capn_ptr p = capn_root(&g_valcapn);
            if (capn_set_text(p, 0, v->v.text))
            {
                fprintf(stderr, "failed to copy text\n");
                exit(2);
            }
            p = capn_getp(p, 0, 1);
            if (p.type == 0)
            {
                break;
            }

            v->ptrval = p;

            bool symbol_provided = (symbol != NULL);
            if (symbol == NULL)
            {
                static struct str buf = STR_INIT;
                v->intval = ++g_valc;
                symbol = strf(&buf, "capn_val%d", (int)v->intval);
            }

            str_addf(&SRC, "%scapn_text %s = {%d,(char*)&capn_buf[%d],(struct capn_segment*)&capn_seg};\n",
                     symbol_provided ? "" : "static ", symbol, p.len - 1, (int)(p.data - p.seg->data - 8));
        }
        break;

    case Value_data:
    case Value__struct:
    case Value_anyPointer:
    case Value__list:
        if (v->v.anyPointer.type != 0)
        {
            capn_ptr p = capn_root(&g_valcapn);
            if (capn_setp(p, 0, v->v.anyPointer))
            {
                fprintf(stderr, "failed to copy object\n");
                exit(2);
            }
            p = capn_getp(p, 0, 1);
            if (p.type == 0)
            {
                break;
            }

            v->ptrval = p;

            bool symbol_provided = (symbol != NULL);
            if (symbol == NULL)
            {
                static struct str buf = STR_INIT;
                v->intval = ++g_valc;
                symbol = strf(&buf, "capn_val%d", (int)v->intval);
            }

            str_addf(&SRC, "%s%s %s = {", symbol_provided ? "" : "static ", v->tname, symbol);
            if (strcmp(v->tname, "capn_ptr") == 0)
                str_addf(&SRC, "{");

            str_addf(&SRC, "%d,%d,%d,%d,%d,%d,%d,(char*)&capn_buf[%d],(struct capn_segment*)&capn_seg", p.type,
                     p.has_ptr_tag, p.is_list_member, p.is_composite_list, p.datasz, p.ptrs, p.len,
                     (int)(p.data - p.seg->data - 8));

            if (strcmp(v->tname, "capn_ptr") == 0)
                str_addf(&SRC, "}");

            str_addf(&SRC, "};\n");
        }
        break;

    case Value__interface:
    case Value__void:
        break;
    default:
        break;
    }
}

static void define_const(struct node *n)
{
    struct value v;
    decode_value(&v, n->n._const.type, n->n._const.value, n->name.str);

    switch (v.v.which)
    {
    case Value__bool:
    case Value_int8:
    case Value_int16:
    case Value_int32:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = %d;\n", v.tname, n->name.str, (int)v.intval);
        break;

    case Value_uint8:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = %u;\n", v.tname, n->name.str, (uint8_t)v.intval);
        break;

    case Value_uint16:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = %u;\n", v.tname, n->name.str, (uint16_t)v.intval);
        break;

    case Value_uint32:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = %u;\n", v.tname, n->name.str, (unsigned int)v.intval);
        break;

    case Value__enum:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = (%s) %u;\n", v.tname, n->name.str, v.tname, (unsigned int)v.intval);
        break;

    case Value_int64:
    case Value_uint64:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        str_addf(&SRC, "%s %s = ((uint64_t) 0x%X << 32) | 0x%X;\n", v.tname, n->name.str, (uint32_t)(v.intval >> 32),
                 (uint32_t)v.intval);
        break;

    case Value_float32:
        str_addf(&HDR, "extern union capn_conv_f32 %s;\n", n->name.str);
        str_addf(&SRC, "union capn_conv_f32 %s = {0x%X};\n", n->name.str, (uint32_t)v.intval);
        break;

    case Value_float64:
        str_addf(&HDR, "extern union capn_conv_f64 %s;\n", n->name.str);
        str_addf(&SRC, "union capn_conv_f64 %s = {((uint64_t) 0x%X << 32) | 0x%X};\n", n->name.str,
                 (uint32_t)(v.intval >> 32), (uint32_t)v.intval);
        break;

    case Value_text:
    case Value_data:
    case Value__struct:
    case Value_anyPointer:
    case Value__list:
        str_addf(&HDR, "extern %s %s;\n", v.tname, n->name.str);
        if (v.ptrval.type == 0)
        {
            str_addf(&SRC, "%s %s;\n", v.tname, n->name.str);
        }
        break;

    case Value__interface:
    case Value__void:
        break;
    default:
        break;
    }

    str_release(&v.tname_buf);
}

static void decode_field(struct field *fields, Field_list l, int i)
{
    struct field f;
    memset(&f, 0, sizeof(f));
    get_Field(&f.f, l, i);

    if (f.f.codeOrder >= capn_len(l))
    {
        fprintf(stderr, "unexpectedly large code order %d >= %d\n", f.f.codeOrder, capn_len(l));
        exit(3);
    }

    if (f.f.which == Field_group)
    {
        f.group = find_node(f.f.group.typeId);
    }

    memcpy(&fields[f.f.codeOrder], &f, sizeof(f));
}

/* RS500: XOR removed — wire stores raw application values. */
static const char *xor_member(struct field *f)
{
    (void)f;
    return "";
}

static const char *ptr_member(struct field *f, const char *var)
{
    static struct str buf = STR_INIT;

    if (f == NULL)
    {
        fprintf(stderr, "Error: NULL field passed to ptr_member\n");
        return "";
    }

    if (f->v.tname == NULL)
    {
        fprintf(stderr, "Error: field has NULL tname in ptr_member\n");
        return var ? var : "";
    }

    if (strcmp(f->v.tname, "capn_ptr") == 0)
    {
        return var;
    }
    else if (var[0] == '*')
    {
        return strf(&buf, "%s->p", var + 1);
    }
    else
    {
        return strf(&buf, "%s.p", var);
    }
}

static void set_member(struct str *func, struct field *f, const char *ptr, const char *tab, const char *var)
{
    if ((func == NULL) || (f == NULL) || (ptr == NULL) || (tab == NULL) || (var == NULL))
    {
        fprintf(stderr, "Error: NULL parameter passed to set_member\n");
        return;
    }

    const char *xor = xor_member(f);
    const char *pvar = ptr_member(f, var);

    if (f->v.t.which == Type__void)
    {
        return;
    }

    str_add(func, tab, -1);

    switch (f->v.t.which)
    {
    case Type__bool:
        str_addf(func, "capn_write1(%s, %d, %s != %d);\n", ptr, (int)f->f.slot.offset, var, (int)f->v.intval);
        break;
    case Type_int8:
        str_addf(func, "capn_write8(%s, %d, (uint8_t) (%s%s));\n", ptr, (int)f->f.slot.offset, var, xor);
        break;
    case Type_int16:
    case Type__enum:
        str_addf(func, "capn_write16(%s, %d, (uint16_t) (%s%s));\n", ptr, (int)(2 * f->f.slot.offset), var, xor);
        break;
    case Type_int32:
        str_addf(func, "capn_write32(%s, %d, (rt_uint32_t) (%s%s));\n", ptr, (int)(4 * f->f.slot.offset), var, xor);
        break;
    case Type_int64:
        str_addf(func, "capn_write64(%s, %d, (uint64_t) (%s%s));\n", ptr, (int)(8 * f->f.slot.offset), var, xor);
        break;
    case Type_uint8:
        str_addf(func, "capn_write8(%s, %d, %s%s);\n", ptr, (int)f->f.slot.offset, var, xor);
        break;
    case Type_uint16:
        str_addf(func, "capn_write16(%s, %d, %s%s);\n", ptr, (int)(2 * f->f.slot.offset), var, xor);
        break;
    case Type_uint32:
        str_addf(func, "capn_write32(%s, %d, %s%s);\n", ptr, (int)(4 * f->f.slot.offset), var, xor);
        break;
    case Type_float32:
        str_addf(func, "capn_write32(%s, %d, capn_from_f32(%s)%s);\n", ptr, (int)(4 * f->f.slot.offset), var, xor);
        break;
    case Type_uint64:
        str_addf(func, "capn_write64(%s, %d, %s%s);\n", ptr, (int)(8 * f->f.slot.offset), var, xor);
        break;
    case Type_float64:
        str_addf(func, "capn_write64(%s, %d, capn_from_f64(%s)%s);\n", ptr, (int)(8 * f->f.slot.offset), var, xor);
        break;
    case Type_text:
        if (f->v.ptrval.type != 0)
        {
            g_val0used = 1;
            str_addf(func, "capn_set_text(%s, %d, (%s.str != capn_val%d.str) ? %s : capn_val0);\n", ptr,
                     (int)f->f.slot.offset, var, (int)f->v.intval, var);
        }
        else
        {
            str_addf(func, "capn_set_text(%s, %d, %s);\n", ptr, (int)f->f.slot.offset, var);
        }
        break;
    case Type_data:
    case Type__struct:
    case Type__interface:
    case Type__list:
    case Type_anyPointer:
        if (f->v.intval == 0)
        {
            str_addf(func, "capn_setp(%s, %d, %s);\n", ptr, (int)f->f.slot.offset, pvar);
        }
        else if (strcmp(f->v.tname, "capn_ptr") == 0)
        {
            g_nullused = 1;
            str_addf(func, "capn_setp(%s, %d, (%s.data != capn_val%d.data) ? %s : capn_null);\n", ptr,
                     (int)f->f.slot.offset, pvar, (int)f->v.intval, pvar);
        }
        else
        {
            g_nullused = 1;
            str_addf(func, "capn_setp(%s, %d, (%s.data != capn_val%d.p.data) ? %s : capn_null);\n", ptr,
                     (int)f->f.slot.offset, pvar, (int)f->v.intval, pvar);
        }
        break;
    default:
        break;
    }
}

static void get_member(struct str *func, struct field *f, const char *ptr, const char *tab, const char *var)
{
    const char *xor = xor_member(f);
    const char *pvar = ptr_member(f, var);

    if (f->v.t.which == Type__void)
    {
        return;
    }

    str_add(func, tab, -1);

    switch (f->v.t.which)
    {
    case Type__bool:
        str_addf(func, "%s = (capn_read8(%s, %d) & %d) != %d;\n", var, ptr, (int)(f->f.slot.offset / 8),
                 1 << (f->f.slot.offset % 8), ((int)f->v.intval) << (f->f.slot.offset % 8));
        return;
    case Type_int8:
        str_addf(func, "%s = (int8_t) ((int8_t)capn_read8(%s, %d))%s;\n", var, ptr, (int)f->f.slot.offset, xor);
        return;
    case Type_int16:
        str_addf(func, "%s = (int16_t) ((int16_t)capn_read16(%s, %d))%s;\n", var, ptr, (int)(2 * f->f.slot.offset),
                 xor);
        return;
    case Type_int32:
        str_addf(func, "%s = (rt_int32_t) ((rt_int32_t)capn_read32(%s, %d))%s;\n", var, ptr,
                 (int)(4 * f->f.slot.offset), xor);
        return;
    case Type_int64:
        str_addf(func, "%s = (int64_t) ((int64_t)(capn_read64(%s, %d))%s);\n", var, ptr, (int)(8 * f->f.slot.offset),
                 xor);
        return;
    case Type_uint8:
        str_addf(func, "%s = capn_read8(%s, %d)%s;\n", var, ptr, (int)f->f.slot.offset, xor);
        return;
    case Type_uint16:
        str_addf(func, "%s = capn_read16(%s, %d)%s;\n", var, ptr, (int)(2 * f->f.slot.offset), xor);
        return;
    case Type_uint32:
        str_addf(func, "%s = capn_read32(%s, %d)%s;\n", var, ptr, (int)(4 * f->f.slot.offset), xor);
        return;
    case Type_uint64:
        str_addf(func, "%s = capn_read64(%s, %d)%s;\n", var, ptr, (int)(8 * f->f.slot.offset), xor);
        return;
    case Type_float32:
        str_addf(func, "%s = capn_to_f32(capn_read32(%s, %d)%s);\n", var, ptr, (int)(4 * f->f.slot.offset), xor);
        return;
    case Type_float64:
        str_addf(func, "%s = capn_to_f64(capn_read64(%s, %d)%s);\n", var, ptr, (int)(8 * f->f.slot.offset), xor);
        return;
    case Type__enum:
        str_addf(func, "%s = (%s)(int) capn_read16(%s, %d)%s;\n", var, f->v.tname, ptr, (int)(2 * f->f.slot.offset),
                 xor);
        return;
    case Type_text:
        if (f->v.intval == 0)
        {
            g_val0used = 1;
        }
        str_addf(func, "%s = capn_get_text(%s, %d, capn_val%d);\n", var, ptr, (int)f->f.slot.offset, (int)f->v.intval);
        return;

    case Type_data:
        str_addf(func, "%s = capn_get_data(%s, %d);\n", var, ptr, (int)f->f.slot.offset);
        break;
    case Type__struct:
    case Type__interface:
    case Type_anyPointer:
    case Type__list:
        str_addf(func, "%s = capn_getp(%s, %d, 0);\n", pvar, ptr, (int)f->f.slot.offset);
        break;
    default:
        return;
    }

    if (f->v.intval != 0)
    {
        str_addf(func, "%sif (!%s.type) {\n", tab, pvar);
        str_addf(func, "%s\t%s = capn_val%d;\n", tab, var, (int)f->v.intval);
        str_addf(func, "%s}\n", tab);
    }
}

struct strings
{
    struct str ftab;
    struct str dtab;
    struct str get;
    struct str set;
    struct str enums;
    struct str decl;
    struct str var;
    struct str pub_get;
    struct str pub_get_header;
    struct str pub_set;
    struct str pub_set_header;
};

static const char *field_name(struct field *f)
{
    static struct str buf = STR_INIT;
    static const char *reserved[] = {
        /* C++11 reserved words */
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "const",
        "constexpr",
        "const_cast",
        "continue",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
        /* COM reserved words */
        "interface",
        "module",
        "import",
        /* capn reserved otherwise Value_ptr enum and type collide */
        "ptr",
        "list",
        /* C11 keywords not reserved in C++ */
        "restrict",
        "_Alignas",
        "_Alignof",
        "_Atomic",
        "_Bool",
        "_Complex",
        "_Generic",
        "_Imaginary",
        "_Noreturn",
        "_Static_assert",
        "_Thread_local",
        /* capn reserved for parameter names */
        "p",
    };

    size_t i;

    /* Add safety checks */
    if (f == NULL)
    {
        strf(&buf, "unknown_field");
        return buf.str;
    }

    const char *orig = f->f.name.str;
    if (orig == NULL)
    {
        orig = "unnamed";
    }

    char *snake = normalize_name(orig, NULL, NAME_FIELD);

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
    {
        if (strcmp(snake, reserved[i]) == 0)
        {
            strf(&buf, "_%s", snake);
            free(snake);
            return buf.str;
        }
    }

    strf(&buf, "%s", snake);
    free(snake);
    return buf.str;
}

static void union_block(struct strings *s, struct field *f)
{
    static struct str buf = STR_INIT;
    str_add(&s->ftab, "\t", -1);
    set_member(&s->set, f, "p.p", s->ftab.str, strf(&buf, "%s%s", s->var.str, field_name(f)));
    get_member(&s->get, f, "p.p", s->ftab.str, strf(&buf, "%s%s", s->var.str, field_name(f)));
    str_addf(&s->set, "%sbreak;\n", s->ftab.str);
    str_addf(&s->get, "%sbreak;\n", s->ftab.str);
    str_setlen(&s->ftab, s->ftab.len - 1);
}

static int32_t in_union(struct field *f)
{
    return (f->f.discriminantValue != 0xFFFF) ? 1 : 0;
}

static void union_cases(struct strings *s, struct node *n, struct field *first_field, int mask)
{
    struct field *f, *u = NULL;

    for (f = first_field; f < n->fields + capn_len(n->n._struct.fields) && in_union(f); f++)
    {

        if (f->f.which != Field_slot)
        {
            continue;
        }
        if ((f->v.ptrval.type != 0) || (f->v.intval != 0))
        {
            continue;
        }
        if ((mask & (1 << f->v.t.which)) == 0)
            continue;

        u = f;
        str_addf(&s->set, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
        str_addf(&s->get, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
    }

    if (u)
    {
        union_block(s, u);
    }
}

static void declare_slot(struct strings *s, struct field *f)
{
    switch (f->v.t.which)
    {
    case Type__void:
        break;
    case Type__bool:
        str_addf(&s->decl, "%s%s %s : 1;\n", s->dtab.str, f->v.tname, field_name(f));
        break;
    default:
        str_addf(&s->decl, "%s%s %s;\n", s->dtab.str, f->v.tname, field_name(f));
        break;
    }
}

static void define_group(struct strings *s, struct node *n, const char *group_name, bool enclose_unions);

static void do_union(struct strings *s, struct node *n, struct field *first_field, const char *union_name)
{
    int32_t tagoff = 2 * n->n._struct.discriminantOffset;
    struct field *f;
    static struct str tag = STR_INIT;
    struct str enums = STR_INIT;

    str_reset(&tag);

    if (union_name != NULL)
    {
        str_addf(&tag, "%.*s_which", s->var.len - 1, s->var.str);
        str_addf(&enums, "enum %s_which {", n->name.str);
        str_addf(&s->decl, "%senum %s_which %s_which;\n", s->dtab.str, n->name.str, union_name);
        str_addf(&s->get, "%s%s = (enum %s_which)(int) capn_read16(p.p, %d);\n", s->ftab.str, tag.str, n->name.str,
                 tagoff);
    }
    else
    {
        str_addf(&tag, "%swhich", s->var.str);
        str_addf(&enums, "enum %s_which {", n->name.str);
        str_addf(&s->decl, "%senum %s_which which;\n", s->dtab.str, n->name.str);
        str_addf(&s->get, "%s%s = (enum %s_which)(int) capn_read16(p.p, %d);\n", s->ftab.str, tag.str, n->name.str,
                 tagoff);
    }

    str_addf(&s->set, "%scapn_write16(p.p, %d, %s);\n", s->ftab.str, tagoff, tag.str);
    str_addf(&s->set, "%sswitch (%s) {\n", s->ftab.str, tag.str);
    str_addf(&s->get, "%sswitch (%s) {\n", s->ftab.str, tag.str);

    /* if we have a bunch of the same C type with zero defaults, we
     * only need to emit one switch block as the layout will line up
     * in the C union */
    union_cases(s, n, first_field, (1 << Type__bool));
    union_cases(s, n, first_field, (1 << Type__enum));
    union_cases(s, n, first_field, (1 << Type_int8) | (1 << Type_uint8));
    union_cases(s, n, first_field, (1 << Type_int16) | (1 << Type_uint16));
    union_cases(s, n, first_field, (1 << Type_int32) | (1 << Type_uint32) | (1 << Type_float32));
    union_cases(s, n, first_field, (1 << Type_int64) | (1 << Type_uint64) | (1 << Type_float64));
    union_cases(s, n, first_field, (1 << Type_text));
    union_cases(s, n, first_field, (1 << Type_data));
    union_cases(s, n, first_field,
                (1 << Type__struct) | (1 << Type__interface) | (1 << Type_anyPointer) | (1 << Type__list));

    str_addf(&s->decl, "%scapnp_nowarn union {\n", s->dtab.str);
    str_add(&s->dtab, "\t", -1);

    /* when we have defaults or groups we have to emit each case seperately */
    for (f = first_field; f < n->fields + capn_len(n->n._struct.fields) && in_union(f); f++)
    {
        if (f > first_field)
        {
            str_addf(&enums, ",");
        }

        str_addf(&enums, "\n\t%s_%s = %d", n->name.str, field_name(f), f->f.discriminantValue);

        switch (f->f.which)
        {
        case Field_group:
            str_addf(&s->get, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
            str_addf(&s->set, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
            str_add(&s->ftab, "\t", -1);
            /* When we add a union inside a union, we need to enclose it in its */
            /* own struct so that its members do not overwrite its own */
            /* discriminant. */
            define_group(s, f->group, field_name(f), true);
            str_addf(&s->get, "%sbreak;\n", s->ftab.str);
            str_addf(&s->set, "%sbreak;\n", s->ftab.str);
            str_setlen(&s->ftab, s->ftab.len - 1);
            break;

        case Field_slot:
            declare_slot(s, f);
            if ((f->v.ptrval.type != 0) || (f->v.intval != 0))
            {
                str_addf(&s->get, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
                str_addf(&s->set, "%scase %s_%s:\n", s->ftab.str, n->name.str, field_name(f));
                union_block(s, f);
            }
            break;

        default:
            break;
        }
    }

    str_setlen(&s->dtab, s->dtab.len - 1);

    if (union_name != NULL)
    {
        str_addf(&s->decl, "%s} %s;\n", s->dtab.str, union_name);
    }
    else
    {
        str_addf(&s->decl, "%s};\n", s->dtab.str);
    }

    str_addf(&s->get, "%sdefault:\n%s\tbreak;\n%s}\n", s->ftab.str, s->ftab.str, s->ftab.str);
    str_addf(&s->set, "%sdefault:\n%s\tbreak;\n%s}\n", s->ftab.str, s->ftab.str, s->ftab.str);

    str_addf(&enums, "\n};\n");
    str_add(&s->enums, enums.str, enums.len);
    str_release(&enums);
}

static void define_field(struct strings *s, struct field *f)
{
    static struct str buf = STR_INIT;

    /* Add safety checks */
    if ((s == NULL) || (f == NULL))
    {
        fprintf(stderr, "Error: NULL parameter passed to define_field\n");
        return;
    }


    switch (f->f.which)
    {
    case Field_slot:
        declare_slot(s, f);
        set_member(&s->set, f, "p.p", s->ftab.str, strf(&buf, "%s%s", s->var.str, field_name(f)));
        get_member(&s->get, f, "p.p", s->ftab.str, strf(&buf, "%s%s", s->var.str, field_name(f)));
        break;

    case Field_group:
        define_group(s, f->group, field_name(f), false);
        break;
    default:
        break;
    }
}

static void define_getter_functions(struct node *node, struct field *field, struct strings *s)
{
    /**
     * define getter
     */
    str_addf(&s->pub_get_header, "\n%s %s_get_%s(%s_ptr p);\n", field->v.tname, node->name.str, field_name(field),
             node->name.str);
    str_addf(&s->pub_get, "\n%s %s_get_%s(%s_ptr p)\n", field->v.tname, node->name.str, field_name(field),
             node->name.str);
    struct str getter_body = STR_INIT;
    get_member(&getter_body, field, "p.p", "", field_name(field));
    str_addf(&s->pub_get, "{\n");
    str_addf(&s->pub_get, "%s%s %s;\n", s->ftab.str, field->v.tname, field_name(field));
    str_addf(&s->pub_get, "%s%s", s->ftab.str, getter_body.str);
    str_release(&getter_body);
    str_addf(&s->pub_get, "%sreturn %s;\n}\n", s->ftab.str, field_name(field));
}

static void define_setter_functions(struct node *node, struct field *field, struct strings *s)
{
    str_addf(&s->pub_set_header, "\nvoid %s_set_%s(%s_ptr p, %s %s);\n", node->name.str, field_name(field),
             node->name.str, field->v.tname, field_name(field));
    str_addf(&s->pub_set, "\nvoid %s_set_%s(%s_ptr p, %s %s)\n", node->name.str, field_name(field), node->name.str,
             field->v.tname, field_name(field));
    struct str setter_body = STR_INIT;
    set_member(&setter_body, field, "p.p", s->ftab.str, field_name(field));
    str_addf(&s->pub_set, "{\n%s}\n", setter_body.str);
    str_release(&setter_body);
}

static void define_group(struct strings *s, struct node *n, const char *group_name, bool enclose_unions)
{
    struct field *f;

    /* Add safety checks */
    if ((s == NULL) || (n == NULL))
    {
        fprintf(stderr, "Error: NULL parameter passed to define_group\n");
        return;
    }


    int32_t flen = capn_len(n->n._struct.fields);
    int32_t ulen = n->n._struct.discriminantCount;


    /* Check if fields array is properly allocated */
    if (flen > 0 && n->fields == NULL)
    {
        fprintf(stderr, "Error: fields array not allocated for node with %d fields\n", flen);
        return;
    }

    /* named union is where all group members are in the union */
    int32_t named_union = ((group_name != NULL) && (ulen == flen) && (ulen > 0)) ? 1 : 0;
    int32_t named_struct = ((group_name != NULL) && (named_union == 0)) ? 1 : 0;
    int32_t empty = 1;

    for (f = n->fields; f < n->fields + flen; f++)
    {
        decode_value(&f->v, f->f.slot.type, f->f.slot.defaultValue, NULL);
        if (f->v.t.which != Type__void)
        {
            empty = 0;
        }
    }

    if (named_struct && empty)
    {
        str_addf(&s->decl, "%s/* struct { -empty- } %s; */\n", s->dtab.str, group_name);
        return;
    }

    if (named_struct)
    {
        str_addf(&s->decl, "%scapnp_nowarn struct {\n", s->dtab.str);
        str_add(&s->dtab, "\t", 1);
    }

    if (group_name != NULL)
    {
        str_addf(&s->var, "%s.", group_name);
    }

    /* fields before the union members */
    for (f = n->fields; f < n->fields + flen && (0 == in_union(f)); f++)
    {
        define_field(s, f);

        if (g_fieldgetset == 0)
        {
            continue;
        }

        if ((n->n.which == Node__struct && n->n._struct.isGroup))
        {
            /* Don't emit in-place getters and setters for groups because they */
            /* are defined as anonymous structs inside their parent struct. */
            /* We could do it, but nested structs shouldn't be accessed */
            /* in-place anyway. */
            continue;
        }

        if (f->v.t.which == Type__void)
        {
            continue;
        }

        define_getter_functions(n, f, s);
        define_setter_functions(n, f, s);
    }

    if (ulen > 0)
    {
        if (enclose_unions)
        {
            /* When we are already inside a union, we need to enclose the union */
            /* with its disciminant. */
            str_addf(&s->decl, "%scapnp_nowarn struct {\n", s->dtab.str);
            str_add(&s->dtab, "\t", 1);
        }

        const bool keep_union_name = named_union && enclose_unions == false;

        do_union(s, n, f, keep_union_name ? group_name : NULL);

        while (f < n->fields + flen && in_union(f))
        {
            f++;
        }

        /* fields after the unnamed union */
        for (; f < n->fields + flen; f++)
        {
            define_field(s, f);
        }

        if (enclose_unions)
        {
            str_setlen(&s->dtab, s->dtab.len - 1);
            str_addf(&s->decl, "%s} %s;\n", s->dtab.str, group_name);
        }
    }

    if (named_struct)
    {
        str_setlen(&s->dtab, s->dtab.len - 1);
        str_addf(&s->decl, "%s} %s;\n", s->dtab.str, group_name);
    }

    if (group_name != NULL)
    {
        /* strlen returns size_t; cast to int for arithmetic with s->var.len */
        str_setlen(&s->var, s->var.len - (int)strlen(group_name) - 1);
    }
}

static void define_struct(struct node *n)
{
    static struct strings s;
    int32_t i;

    /* Add safety check for node structure */
    if (n == NULL)
    {
        fprintf(stderr, "Error: NULL node passed to define_struct\n");
        return;
    }

    /* Generate struct name with capn_ prefix: capn_system_ooc_cfg_t */
    char *base_name = normalize_name(n->name.str, "", NAME_STRUCT);
    size_t sn_buflen = strlen(base_name) + 10;
    char *struct_name = malloc(sn_buflen);
    if (struct_name == NULL)
    {
        fprintf(stderr, "Error: malloc failed for struct_name\n");
        free(base_name);
        exit(EXIT_FAILURE);
    }
    snprintf(struct_name, sn_buflen, "capn_%s", base_name);
    free(base_name);

    str_reset(&s.dtab);
    str_reset(&s.ftab);
    str_reset(&s.get);
    str_reset(&s.set);
    str_reset(&s.enums);
    str_reset(&s.decl);
    str_reset(&s.var);
    str_reset(&s.pub_get);
    str_reset(&s.pub_set);
    str_reset(&s.pub_get_header);
    str_reset(&s.pub_set_header);

    str_add(&s.dtab, "\t", -1);
    str_add(&s.ftab, "\t", -1);
    str_add(&s.var, "s->", -1);

    define_group(&s, n, NULL, false);

    str_addf(&HDR, "\n%stypedef struct %s {\n", s.decl.len == 0 ? "capnp_nowarn " : "", struct_name);
    str_add(&HDR, s.decl.str, s.decl.len);
    str_addf(&HDR, "} %s;\n", struct_name);

    for (i = capn_len(n->n.annotations) - 1; i >= 0; i--)
    {
        struct Annotation a;
        struct Value v;
        get_Annotation(&a, n->n.annotations, i);
        read_Value(&v, a.value);

        switch (a.id)
        {
        case 0xcefaf27713042144UL:
            if (v.which != Value_text)
            {
                fprintf(stderr, "schema breakage on $C::typedefto annotation\n");
                exit(2);
            }

            str_addf(&HDR, "\ntypedef %s %s;\n", struct_name, v.text.str);
            break;
        default:
            break;
        }
    }

    /* function implementations use normalized names */
    /* Use empty module for base to avoid duplicate prefix in function/constant names */
    char *base = normalize_name(n->name.str, "", NAME_FUNC);
    struct str ptr_t = STR_INIT;
    str_addf(&ptr_t, "capn_%s_ptr", base);
    struct str list_t = STR_INIT;
    str_addf(&list_t, "capn_%s_list", base);

    str_addf(&SRC, "\n%s new_%s(struct capn_segment *s) {\n", ptr_t.str, base);
    str_addf(&SRC, "\t%s p;\n", ptr_t.str);
    str_addf(&SRC, "\tp.p = capn_new_struct(s, %d, %d);\n", 8 * n->n._struct.dataWordCount, n->n._struct.pointerCount);
    str_addf(&SRC, "\treturn p;\n");
    str_addf(&SRC, "}\n");

    /* struct size constants */
    str_addf(&HDR, "\nstatic const rt_uint32_t %s_word_count = %d;\n", base, n->n._struct.dataWordCount);
    str_addf(&HDR, "\nstatic const rt_uint32_t %s_pointer_count = %d;\n", base, n->n._struct.pointerCount);
    str_addf(&HDR, "\nstatic const rt_uint32_t %s_struct_bytes_count = %d;\n\n", base,
             8 * (n->n._struct.pointerCount + n->n._struct.dataWordCount));

    str_addf(&SRC, "%s new_%s_list(struct capn_segment *s, rt_int32_t len) {\n", list_t.str, base);
    str_addf(&SRC, "\t%s p;\n", list_t.str);
    str_addf(&SRC, "\tp.p = capn_new_list(s, len, %d, %d);\n", 8 * n->n._struct.dataWordCount,
             n->n._struct.pointerCount);
    str_addf(&SRC, "\treturn p;\n");
    str_addf(&SRC, "}\n");

    str_addf(&SRC, "void read_%s(%s *s capnp_unused, %s p) {\n", base, struct_name, ptr_t.str);
    str_addf(&SRC, "\tcapn_resolve(&p.p);\n\tcapnp_use(s);\n");
    str_add(&SRC, s.get.str, s.get.len);
    str_addf(&SRC, "}\n");

    str_addf(&SRC, "void write_%s(const %s *s capnp_unused, %s p) {\n", base, struct_name, ptr_t.str);
    str_addf(&SRC, "\tcapn_resolve(&p.p);\n\tcapnp_use(s);\n");
    str_add(&SRC, s.set.str, s.set.len);
    str_addf(&SRC, "}\n");

    str_addf(&SRC, "void get_%s(%s *s, %s l, rt_int32_t i) {\n", base, struct_name, list_t.str);
    str_addf(&SRC, "\t%s p;\n", ptr_t.str);
    str_addf(&SRC, "\tp.p = capn_getp(l.p, i, 0);\n");
    str_addf(&SRC, "\tread_%s(s, p);\n", base);
    str_addf(&SRC, "}\n");

    str_addf(&SRC, "void set_%s(const %s *s, %s l, rt_int32_t i) {\n", base, struct_name, list_t.str);
    str_addf(&SRC, "\t%s p;\n", ptr_t.str);
    str_addf(&SRC, "\tp.p = capn_getp(l.p, i, 0);\n");
    str_addf(&SRC, "\twrite_%s(s, p);\n", base);
    str_addf(&SRC, "}\n");

    str_add(&SRC, s.pub_get.str, s.pub_get.len);
    str_add(&SRC, s.pub_set.str, s.pub_set.len);

    str_add(&HDR, s.pub_get_header.str, s.pub_get_header.len);
    str_add(&HDR, s.pub_set_header.str, s.pub_set_header.len);

    str_release(&ptr_t);
    str_release(&list_t);
    free(base);
    free(struct_name);
}

#if 0
/* Commenting out interfaces until the RPC protocol has been spec'd */
static int find_offset(struct str *v, int inc, uint64_t mask) {
	int i, j;
	union {uint64_t u; char c[8];} umask;
	umask.u = capn_flip64(mask);
	for (i = 0; i < v->len*8; i += inc) {
		for (j = i; j < i+inc; j++) {
			if (((uint8_t*)v->str)[j/8] & (1 << (j%8))) {
				goto loop;
			}
		}

		for (j = i; j < i+inc; j++) {
			((uint8_t*)v->str)[j/8] |= 1 << (j%8);
		}

		return j/inc;
loop:
		continue;
	}
	str_add(v, umask.c, 8);
	return (v->len-8)/inc;
}

static void define_method(struct node *iface, int ord) {
	static struct str buf = STR_INIT;
	struct member *mbrs;
	struct InterfaceNode_Method method;
	int i, ptrs = 0, datasz;

	get_InterfaceNode_Method(&method, iface->i.methods, ord);
	mbrs = calloc(method.params.p.len, sizeof(*mbrs));

	str_reset(&buf);

	for (i = 0; i < method.params.p.len; i++) {
		struct InterfaceNode_Method_Param param;
		struct member *m = &mbrs[i];

		get_InterfaceNode_Method_Param(&param, method.params, i);
		decode_value(&m->v, param.type, param.defaultValue, NULL);
		m->m.name = param.name;
		m->m.annotations = param.annotations;
		m->m.ordinal = i;

		switch (m->v.t.body_tag) {
		case Type__void:
			break;
		case Type__bool:
			m->f.offset = find_offset(&buf, 1, 1);
			break;
		case Type_int8:
		case Type_uint8:
			m->f.offset = find_offset(&buf, 8, 0xFF);
			break;
		case Type_int16:
		case Type_uint16:
		case Type__enum:
			m->f.offset = find_offset(&buf, 16, 0xFFFF);
			break;
		case Type_int32:
		case Type_uint32:
		case Type_float32:
			m->f.offset = find_offset(&buf, 32, 0xFFFFFFFFu);
			break;
		case Type_int64:
		case Type_uint64:
		case Type_float64:
			m->f.offset = find_offset(&buf, 64, ~((uint64_t) 0));
			break;
		case Type_text:
		case Type_data:
		case Type__list:
		case Type__struct:
		case Type__interface:
		case Type_anyPointer:
			m->f.offset = ptrs++;
			break;
		}
	}

	datasz = buf.len;


	/* write function to initiate a call */

	str_addf(&HDR, "\nint write_%s_%s(struct capn_msg*", iface->name.str, method.name.str);
	str_addf(&SRC, "\nint write_%s_%s(struct capn_msg *m", iface->name.str, method.name.str);
	for (i = 0; i < method.params.p.len; i++) {
		struct member *m = &mbrs[i];
		str_addf(&HDR, ", %s %s", m->v.tname, m->m.name.str);
		str_addf(&SRC, ", %s a%d", m->v.tname, i);
	}
	str_addf(&HDR, ");\n");
	str_addf(&SRC, ") {\n");

	str_addf(&SRC, "\tint err = 0;\n");
	str_addf(&SRC, "\tm->method = %d;\n", ord);
	str_addf(&SRC, "\tm->iface = ((uint64_t) %#xu << 32) | %#xu;\n", (uint32_t) (iface->n.id >> 32), (uint32_t) iface->n.id);

	if (datasz || ptrs) {
		str_addf(&SRC, "\tm->args = capn_new_struct(m->seg, %d, %d);\n", datasz, ptrs);
	} else {
		g_nullused = 1;
		str_addf(&SRC, "\tm->args = capn_null;\n");
	}

	for (i = 0; i < method.params.p.len; i++) {
		set_member(&mbrs[i], "m->args", "\t", strf(&buf, "a%d", i));
	}

	str_addf(&SRC, "\treturn err;\n");
	str_addf(&SRC, "}\n");


	/* read function to handle a call */

	if (datasz || ptrs) {
		str_addf(&HDR, "void read_%s_%s(struct capn_msg*", iface->name.str, method.name.str);
		str_addf(&SRC, "void read_%s_%s(struct capn_msg *m", iface->name.str, method.name.str);
		for (i = 0; i < method.params.p.len; i++) {
			struct member *m = &mbrs[i];
			str_addf(&HDR, ", %s *%s", m->v.tname, m->m.name.str);
			str_addf(&SRC, ", %s *a%d", m->v.tname, i);
		}
		str_addf(&HDR, ");\n");
		str_addf(&SRC, ") {\n");

		for (i = 0; i < method.params.p.len; i++) {
			get_member(&mbrs[i], "m->args", "\t", strf(&buf, "*a%d", i));
		}

		str_addf(&SRC, "}\n");
	}

	free(mbrs);
}
#endif

static void declare(struct node *file_node, const char *format, int num)
{
    struct node *n;
    str_addf(&HDR, "\n");
    for (n = file_node->file_nodes; n != NULL; n = n->next_file_node)
    {
        if (n->n.which == Node__struct && n->n._struct.isGroup == 0)
        {
            /* Use empty module to avoid duplicate prefix in function names */
            char *base = normalize_name(n->name.str, "", NAME_FUNC);
            /* struct_name needs capn_ prefix for struct typedef */
            char *base_for_struct = normalize_name(n->name.str, "", NAME_STRUCT);
            size_t sn_len = strlen(base_for_struct) + 10;
            char *struct_name = malloc(sn_len);
            if (struct_name == NULL)
            {
                fprintf(stderr, "Error: malloc failed for struct_name in declare()\n");
                free(base);
                free(base_for_struct);
                exit(EXIT_FAILURE);
            }
            snprintf(struct_name, sn_len, "capn_%s", base_for_struct);

            /* ptr_type_name needs module_ prefix for ptr/list typedef */
            const char *mod = (g_module_name != NULL) ? g_module_name : "";
            size_t pt_len = strlen(mod) + strlen(base_for_struct) + 10;
            char *ptr_type_name = malloc(pt_len);
            if (ptr_type_name == NULL)
            {
                fprintf(stderr, "Error: malloc failed for ptr_type_name in declare()\n");
                free(base);
                free(base_for_struct);
                free(struct_name);
                exit(EXIT_FAILURE);
            }
            if ((g_module_name != NULL) && (g_module_name[0] != '\0'))
            {
                snprintf(ptr_type_name, pt_len, "%s_%s", g_module_name, base_for_struct);
            }
            else
            {
                snprintf(ptr_type_name, pt_len, "capn_%s", base_for_struct);
            }
            free(base_for_struct);

            switch (num)
            {
            case 3:
                /* format like: "void read_%s(%s*, %s_ptr);\n" */
                /* args: base, struct_name, ptr_type_name */
                /* but we need capn_base format */
                {
                    size_t cb_len = strlen(base) + 10;
                    char *capn_base = malloc(cb_len);
                    if (capn_base == NULL)
                    {
                        fprintf(stderr, "Error: malloc failed for capn_base in declare()\n");
                        free(base);
                        free(struct_name);
                        free(ptr_type_name);
                        exit(EXIT_FAILURE);
                    }
                    snprintf(capn_base, cb_len, "capn_%s", base);
                    str_addf(&HDR, format, base, struct_name, capn_base);
                    free(capn_base);
                }
                break;
            case 2:
                /* format may be: */
                /* "typedef struct {capn_ptr p;} %s_ptr;\n" (one %s) */
                /* "%s_ptr new_%s(struct capn_segment*);\n" (two %s) */
                /* In both cases, use capn_base */
                {
                    size_t cb_len = strlen(base) + 10;
                    char *capn_base = malloc(cb_len);
                    if (capn_base == NULL)
                    {
                        fprintf(stderr, "Error: malloc failed for capn_base in declare()\n");
                        free(base);
                        free(struct_name);
                        free(ptr_type_name);
                        exit(EXIT_FAILURE);
                    }
                    snprintf(capn_base, cb_len, "capn_%s", base);
                    /* Count %s in format string */
                    int32_t count = 0;
                    const char *p = format;
                    while ((p = strstr(p, "%s")) != NULL)
                    {
                        count++;
                        p += 2;
                    }
                    if (count == 1)
                    {
                        str_addf(&HDR, format, capn_base);
                    }
                    else
                    {
                        /* Two args: capn_base (type name), base (func name) */
                        str_addf(&HDR, format, capn_base, base);
                    }
                    free(capn_base);
                }
                break;
            case 1:
                str_addf(&HDR, format, struct_name);
                break;
            default:
                break;
            }

            free(ptr_type_name);
            free(base);
            free(struct_name);
        }
    }
}

#define ANNOTATION_NAMESPACE 0xf2c035025fec7c2bUL

int main()
{
    struct capn capn;
    CodeGeneratorRequest_ptr root;
    struct CodeGeneratorRequest req;
    struct node *file_node, *n, *f;
    struct node *all_files = NULL, *all_structs = NULL;
    struct id_bst *used_import_ids = NULL;
    int32_t i, j;
    uint64_t total_len = 0;
    struct capn_segment *current_seg;

    g_module_name = NULL; /* Initialize module name */

#ifdef _WIN32
    /* Windows requires binary mode for stdin/stdout to prevent CRLF conversion */
    _setmode(STDIN_FILENO, _O_BINARY);
    _setmode(STDOUT_FILENO, _O_BINARY);
#endif

    if (capn_init_fp(&capn, stdin, 0) == -1)
    {
        fprintf(stderr, "failed to read schema from stdin\n");
        return -1;
    }
    current_seg = capn.seglist;
    do
    {
        total_len += current_seg->len;
        current_seg = current_seg->next;
    } while (current_seg);

    g_valseg.data = calloc(1, total_len);
    if (!g_valseg.data)
    {
        fprintf(stderr, "Error: calloc failed for g_valseg.data\n");
        return -1;
    }
    g_valseg.cap = total_len;
    root.p = capn_getp(capn_root(&capn), 0, 1);
    read_CodeGeneratorRequest(&req, root);
    for (i = 0; i < capn_len(req.nodes); i++)
    {
        n = calloc(1, sizeof(*n));
        if (n == NULL)
        {
            fprintf(stderr, "Error: calloc failed for node allocation\n");
            return -1;
        }
        get_Node(&n->n, req.nodes, i);
        insert_node(n);

        switch (n->n.which)
        {
        case Node_file:
            n->next = all_files;
            all_files = n;
            break;

        case Node__struct:
            n->next = all_structs;
            all_structs = n;
            break;

        default:
            break;
        }
    }
    for (n = all_structs; n != NULL; n = n->next)
    {
        n->fields = calloc(capn_len(n->n._struct.fields), sizeof(n->fields[0]));
        if (n->fields == NULL && capn_len(n->n._struct.fields) > 0)
        {
            fprintf(stderr, "Error: calloc failed for struct fields allocation\n");
            return -1;
        }
        for (j = 0; j < capn_len(n->n._struct.fields); j++)
        {
            decode_field(n->fields, n->n._struct.fields, j);
        }
    }
    for (n = all_files; n != NULL; n = n->next)
    {
        struct str b = STR_INIT;
        const char *namespace = NULL;

        /* apply name space if present */
        for (j = capn_len(n->n.annotations) - 1; j >= 0; j--)
        {
            struct Annotation a;
            struct Value v;
            get_Annotation(&a, n->n.annotations, j);
            read_Value(&v, a.value);

            if (a.id == ANNOTATION_NAMESPACE)
            {
                if (v.which != Value_text)
                {
                    fprintf(stderr, "%s: schema breakage on $C::namespace annotation\n", n->n.displayName.str);
                    exit(2);
                }
                if (namespace != NULL)
                {
                    fprintf(stderr, "%s: $C::namespace annotation appears more than once.\n", n->n.displayName.str);
                    exit(2);
                }
                namespace = v.text.str ? v.text.str : "";
            }
        }

        if (namespace == NULL)
        {
            namespace = "";
        }

        for (i = capn_len(n->n.nestedNodes) - 1; i >= 0; i--)
        {
            struct Node_NestedNode nest;
            get_Node_NestedNode(&nest, n->n.nestedNodes, i);
            struct node *nn = find_node_mayfail(nest.id);
            if (nn != NULL)
            {
                resolve_names(&b, nn, nest.name, n, namespace);
            }
        }

        str_release(&b);
    }
    /* find all the used imports */
    for (n = all_structs; n != NULL; n = n->next)
    {
        char *display_name = strdup(n->n.displayName.str);
        if (display_name == NULL)
        {
            fprintf(stderr, "Error: strdup failed for display_name\n");
            exit(EXIT_FAILURE);
        }
        char *file_name = strtok(display_name, ":");

        if (file_name == NULL)
        {
            fprintf(stderr, "Unable to determine file name for struct node: %s\n", n->n.displayName.str);
            exit(2);
        }

        /* find the file node corresponding to the file name */
        for (f = all_files; f != NULL; f = f->next)
        {
            if (strcmp(file_name, f->n.displayName.str) == 0)
                break;
        }

        if (f == NULL)
        {
            fprintf(stderr, "Unable to find file node with file name: %s\n", file_name);
            exit(2);
        }

        /* mark this import as used */
        if (contains_id(used_import_ids, f->n.id) == false)
            used_import_ids = insert_id(used_import_ids, f->n.id);

        free(display_name);
    }
    for (i = 0; i < capn_len(req.requestedFiles); i++)
    {

        /* Free previous module name to prevent memory leak */
        if (g_module_name != NULL)
        {
            free((void *)g_module_name);
            g_module_name = NULL;
        }

        struct CodeGeneratorRequest_RequestedFile file_req;
        static struct str b = STR_INIT;
        char *p;
        const char *nameinfix = NULL;
        FILE *srcf, *hdrf;
        struct id_bst *donotinclude_ids = NULL;

        g_valc = 0;
        g_valseg.len = 0;
        g_val0used = 0;
        g_nullused = 0;
        capn_init_malloc(&g_valcapn);
        capn_append_segment(&g_valcapn, &g_valseg);

        get_CodeGeneratorRequest_RequestedFile(&file_req, req.requestedFiles, i);
        file_node = find_node(file_req.id);
        if (file_node == NULL)
        {
            fprintf(stderr, "invalid file_node specified\n");
            exit(2);
        }

        /* Set module name from file name */
        char *module_name = NULL;
        if (file_node->n.displayName.str != NULL)
        {
            module_name = strdup(file_node->n.displayName.str);
            if (module_name == NULL)
            {
                fprintf(stderr, "Error: strdup failed for module_name\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            module_name = strdup("unknown");
            if (module_name == NULL)
            {
                fprintf(stderr, "Error: strdup failed for default module_name\n");
                exit(EXIT_FAILURE);
            }
        }
        char *dot = (module_name != NULL) ? strrchr(module_name, '.') : NULL;
        if (dot != NULL)
            *dot = '\0';
        char *slash = (module_name != NULL) ? strrchr(module_name, '/') : NULL;
        if (slash != NULL)
        {
            char *temp = strdup(slash + 1);
            if (temp == NULL)
            {
                fprintf(stderr, "Error: strdup failed for temp module_name\n");
                free(module_name);
                exit(EXIT_FAILURE);
            }
            free(module_name);
            module_name = temp;
        }
        g_module_name = module_name;

        for (j = capn_len(file_node->n.annotations) - 1; j >= 0; j--)
        {
            struct Annotation a;
            struct Value v;
            get_Annotation(&a, file_node->n.annotations, j);
            read_Value(&v, a.value);

            switch (a.id)
            {
            case 0x85a8d86d736ba637UL: /* $C::nameinfix */
                if (v.which != Value_text)
                {
                    fprintf(stderr, "schema breakage on $C::nameinfix annotation\n");
                    exit(2);
                }
                if (nameinfix != NULL)
                {
                    fprintf(stderr, "$C::nameinfix annotation appears more than once\n");
                    exit(2);
                }
                nameinfix = v.text.str ? v.text.str : "";
                break;
            case 0xf72bc690355d66deUL: /* $C::fieldgetset */
                g_fieldgetset = 1;
                break;
            case 0x8c99797357b357e9UL: /* $C::donotinclude */
                if (v.which != Value_uint64)
                {
                    fprintf(stderr, "schema breakage on $C::donotinclude annotation\n");
                    exit(2);
                }
                donotinclude_ids = insert_id(donotinclude_ids, v.uint64);
                break;
            default:
                break;
            }
        }
        if (nameinfix == NULL)
        {
            nameinfix = "";
        }

        str_reset(&HDR);
        str_reset(&SRC);

        /* Generate header guard from filename instead of random ID */
        char *header_guard = generate_header_guard(file_node->n.displayName.str);
        str_addf(&HDR, "#ifndef %s\n", header_guard);
        str_addf(&HDR, "#define %s\n", header_guard);
        free(header_guard);
        str_addf(&HDR, "/* AUTO GENERATED - DO NOT EDIT */\n");
        str_addf(&HDR, "#include <capnp_c.h>\n");
        str_addf(&HDR, "#include <rtdef.h>\n\n");
        str_addf(&HDR, "#if CAPN_VERSION != 1\n");
        str_addf(&HDR, "#error \"version mismatch between capnp_c.h and generated code\"\n");
        str_addf(&HDR, "#endif\n\n");
        str_addf(&HDR, "#ifndef capnp_nowarn\n"
                       "# ifdef __GNUC__\n"
                       "#  define capnp_nowarn __extension__\n"
                       "# else\n"
                       "#  define capnp_nowarn\n"
                       "# endif\n"
                       "#endif\n\n");

        for (j = 0; j < capn_len(file_req.imports); j++)
        {
            struct CodeGeneratorRequest_RequestedFile_Import im;
            get_CodeGeneratorRequest_RequestedFile_Import(&im, file_req.imports, j);

            int32_t skip_dni = contains_id(donotinclude_ids, im.id);
            int32_t used = contains_id(used_import_ids, im.id);
            if (skip_dni != 0)
            {
                continue;
            }
            if (used == 0)
            {
                continue;
            }

            const char *base_path = im.name.str[0] == '/' ? &im.name.str[1] : im.name.str;
            str_addf(&HDR, "#include \"%s%s.h\"\n", base_path, nameinfix);
        }

        /* free only donotinclude_ids per file; used_import_ids freed after loop */
        free_id_bst(donotinclude_ids);

        str_addf(&HDR, "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n");

        /* Struct typedefs are generated by define_struct(), not here */
        /* declare(file_node, "typedef struct %s %s_t;\n", 2); */
        declare(file_node, "typedef struct {capn_ptr p;} %s_ptr;\n", 2);
        declare(file_node, "typedef struct {capn_ptr p;} %s_list;\n", 2);

        for (n = file_node->file_nodes; n != NULL; n = n->next_file_node)
        {
            if (n->n.which == Node__enum)
            {
                define_enum(n);
            }
        }
        for (n = file_node->file_nodes; n != NULL; n = n->next_file_node)
        {
            if (n->n.which == Node__const)
            {
                define_const(n);
            }
        }

        for (n = file_node->file_nodes; n != NULL; n = n->next_file_node)
        {
            if (n->n.which == Node__struct && n->n._struct.isGroup == 0)
            {
                define_struct(n);
            }
        }

        declare(file_node, "%s_ptr new_%s(struct capn_segment*);\n", 2);
        declare(file_node, "%s_list new_%s_list(struct capn_segment*, rt_int32_t len);\n", 2);
        declare(file_node, "void read_%s(%s*, %s_ptr);\n", 3);
        declare(file_node, "void write_%s(const %s*, %s_ptr);\n", 3);
        declare(file_node, "void get_%s(%s*, %s_list, rt_int32_t i);\n", 3);
        declare(file_node, "void set_%s(const %s*, %s_list, rt_int32_t i);\n", 3);

        str_addf(&HDR, "\n#ifdef __cplusplus\n}\n#endif\n#endif\n");

        /* write out the header */
        hdrf = fopen(strf(&b, "%s%s.h", file_node->n.displayName.str, nameinfix), "w");
        if (hdrf == NULL)
        {
            fprintf(stderr, "failed to open %s: %s\n", b.str, strerror(errno));
            exit(2);
        }
        fwrite(HDR.str, 1, HDR.len, hdrf);
        fclose(hdrf);

        /* write out the source */

        srcf = fopen(strf(&b, "%s%s.c", file_node->n.displayName.str, nameinfix), "w");
        if (srcf == NULL)
        {
            fprintf(stderr, "failed to open %s: %s\n", b.str, strerror(errno));
            exit(2);
        }
        p = strrchr(file_node->n.displayName.str, '/');
        fprintf(srcf, "#include \"%s%s.h\"\n", p ? p + 1 : file_node->n.displayName.str, nameinfix);
        fprintf(srcf, "#include <rtdef.h>\n");
        fprintf(srcf, "/* AUTO GENERATED - DO NOT EDIT */\n");
        fprintf(srcf, "#ifdef __GNUC__\n"
                      "# define capnp_unused __attribute__((unused))\n"
                      "# define capnp_use(x) (void) x;\n"
                      "#else\n"
                      "# define capnp_unused\n"
                      "# define capnp_use(x)\n"
                      "#endif\n\n");

        if (g_val0used)
        {
            fprintf(srcf, "static const capn_text capn_val0 = {0,\"\",0};\n");
        }
        if (g_nullused)
        {
            fprintf(srcf, "static const capn_ptr capn_null = {CAPN_NULL};\n");
        }

        if (g_valseg.len > 8)
        {
            size_t k;
            fprintf(srcf, "static const uint8_t capn_buf[%zu] = {", g_valseg.len - 8);
            for (k = 8; k < g_valseg.len; k++)
            {
                if (k > 8)
                {
                    fprintf(srcf, ",");
                }
                if ((k % 8) == 0)
                    fprintf(srcf, "\n\t");
                fprintf(srcf, "%u", ((uint8_t *)g_valseg.data)[k]);
            }
            fprintf(srcf, "\n};\n");

            fprintf(srcf, "static const struct capn_segment capn_seg = {{0},0,0,0,(char*)&capn_buf[0],%zu,%zu,0};\n",
                    g_valseg.len - 8, g_valseg.len - 8);
        }

        fwrite(SRC.str, 1, SRC.len, srcf);
        fclose(srcf);

        capn_free(&g_valcapn);
    }

    /* Free module name from last iteration */
    if (g_module_name != NULL)
    {
        free((void *)g_module_name);
    }

    /* free after processing all files */
    free_id_bst(used_import_ids);

    /* Free global value segment data */
    free(g_valseg.data);

    return 0;
}
