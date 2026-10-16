#include "cmdr.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cmdr_value *value;
    int release; /* Free this list's arrays after its children. */
} free_frame;

static int free_push(free_frame **stack, size_t *cap, size_t *top,
                     cmdr_value *value, int release)
{
    if (*top == *cap) {
        size_t next = *cap ? *cap * 2 : 16;
        free_frame *grown = realloc(*stack, next * sizeof(*grown));
        if (!grown) return 0;
        *stack = grown;
        *cap = next;
    }
    free_frame *frame = &(*stack)[(*top)++];
    frame->value = value;
    frame->release = release;
    return 1;
}

void cmdr_value_free(cmdr_value *value)
{
    if (!value) return;
    /* Iterative post-order free (growable heap stack) so deeply nested values
     * cannot overflow the C stack. Only a real out-of-memory condition can
     * interrupt the walk; it then leaks the remainder instead of crashing. */
    free_frame *stack = NULL;
    size_t cap = 0, top = 0;
    cmdr_value *current = value;
    for (;;) {
        if (current->kind == CMDR_LIST) {
            if (!free_push(&stack, &cap, &top, current, 1)) { free(stack); return; }
            for (size_t i = 1; i < current->count; ++i)
                if (!free_push(&stack, &cap, &top, &current->items[i], 0)) {
                    free(stack);
                    return;
                }
            current = current->count ? &current->items[0] : NULL;
        } else {
            free(current->text);
            *current = (cmdr_value){0};
            current = NULL;
        }
        while (!current && top) {
            free_frame frame = stack[--top];
            if (frame.release) {
                free(frame.value->items);
                *frame.value = (cmdr_value){0};
            } else {
                current = frame.value;
            }
        }
        if (!current && !top) break;
    }
    free(stack);
}

int cmdr_value_text(cmdr_value *out, const char *text, char **error)
{
    size_t n = strlen(text);
    char *copy = malloc(n + 1);
    if (!copy) return cmdr_error(error, "out of memory");
    memcpy(copy, text, n + 1);
    *out = (cmdr_value){.kind = CMDR_TEXT, .text = copy};
    return 0;
}

int cmdr_value_push(cmdr_value *list, cmdr_value *item, char **error)
{
    if (list->kind != CMDR_LIST) return cmdr_error(error, "expected a list");
    if (list->count >= SIZE_MAX / sizeof(*list->items))
        return cmdr_error(error, "list is too large");
    cmdr_value *next = realloc(list->items, (list->count + 1) * sizeof(*next));
    if (!next) return cmdr_error(error, "out of memory");
    list->items = next;
    list->items[list->count++] = *item;
    *item = (cmdr_value){0};
    return 0;
}

static int clone_value(const cmdr_value *value, cmdr_value *out, char **error,
                       unsigned depth)
{
    if (depth > CMDR_MAX_VALUE_DEPTH)
        return cmdr_error(error, "value is nested too deeply");
    *out = (cmdr_value){.kind = value->kind};
    if (value->kind == CMDR_TEXT || value->kind == CMDR_FAILURE) {
        if (cmdr_value_text(out, value->text, error)) return 1;
        out->kind = value->kind;
    } else if (value->kind == CMDR_LIST) {
        for (size_t i = 0; i < value->count; ++i) {
            cmdr_value item = {0};
            if (clone_value(&value->items[i], &item, error, depth + 1) ||
                cmdr_value_push(out, &item, error)) {
                cmdr_value_free(&item);
                cmdr_value_free(out);
                return 1;
            }
        }
    }
    return 0;
}

int cmdr_value_clone(const cmdr_value *value, cmdr_value *out, char **error)
{
    return clone_value(value, out, error, 0);
}

static int rendered_size(const cmdr_value *v, size_t *n, char **error,
                         unsigned depth)
{
    if (depth > CMDR_MAX_VALUE_DEPTH)
        return cmdr_error(error, "value is nested too deeply");
    if (v->kind == CMDR_FAILURE) return cmdr_error(error, "%s", v->text);
    if (v->kind == CMDR_TEXT) {
        size_t length = strlen(v->text);
        if (length > SIZE_MAX - *n - 1) return cmdr_error(error, "value is too large");
        *n += length;
    } else if (v->kind == CMDR_LIST) {
        for (size_t i = 0; i < v->count; ++i) {
            if (rendered_size(&v->items[i], n, error, depth + 1)) return 1;
            if (*n >= SIZE_MAX - 1) return cmdr_error(error, "value is too large");
            ++*n;
        }
    }
    return 0;
}

/* Only called after rendered_size() accepted the tree, so recursion is bounded
 * by CMDR_MAX_VALUE_DEPTH and cannot overflow the stack. */
static char *render(const cmdr_value *v, char *p)
{
    if (v->kind == CMDR_TEXT) {
        size_t n = strlen(v->text);
        memcpy(p, v->text, n);
        p += n;
    } else if (v->kind == CMDR_LIST) {
        for (size_t i = 0; i < v->count; ++i) {
            p = render(&v->items[i], p);
            *p++ = '\n';
        }
    }
    return p;
}

int cmdr_value_render(const cmdr_value *value, char **text, char **error)
{
    *text = NULL;
    if (value->kind == CMDR_NONE) return 0;
    size_t n = 0;
    if (rendered_size(value, &n, error, 0)) return 1;
    *text = malloc(n + 1);
    if (!*text) return cmdr_error(error, "out of memory");
    *render(value, *text) = '\0';
    return 0;
}
