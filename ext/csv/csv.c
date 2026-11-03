/* CSV, as Python's csv module with the excel dialect: '"' quotes a field,
 * '""' is a quote inside one, and a row ends at \r\n, \n, or \r. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

typedef struct {
    char *data;
    size_t used, size;
} buffer;

static void put(buffer *b, const char *text, size_t n)
{
    if (b->used + n + 1 > b->size) {
        while (b->used + n + 1 > b->size) b->size = b->size ? b->size * 2 : 64;
        b->data = cmdr_realloc(b->data, b->size, 1);
    }
    memcpy(b->data + b->used, text, n);
    b->used += n;
    b->data[b->used] = '\0';
}

/* The optional DELIMITER, argv[at], or ','. */
static int delimiter(const char *name, size_t argc, const cmdr_value *argv, size_t at, char *out, char **error)
{
    *out = ',';
    if (argc <= at) return 0;
    const char *d = cmdr_text_of(&argv[at]);
    if (!d || strlen(d) != 1 || strchr("\"\r\n", d[0]))
        return cmdr_error(error, "%s: DELIMITER must be one byte other than a quote or line break", name);
    *out = d[0];
    return 0;
}

static void end_field(cmdr_value *row, buffer *field)
{
    cmdr_push(row, cmdr_text(field->data ? field->data : "", field->used));
    field->used = 0;
}

/* Rows as lists of text. A blank line is an empty row, as in Python. A quote
 * that is never closed, or text after a closing quote, is an error. */
static int parse(const char *name, const char *text, char delim, cmdr_value *rows, char **error)
{
    *rows = cmdr_list();
    buffer field = {0};
    size_t line = 1;
    const char *p = text;
    while (*p) {
        cmdr_value row = cmdr_list();
        size_t start = line;
        int blank = *p == '\r' || *p == '\n';
        while (!blank) {
            if (*p == '"') {
                for (++p;; ++p) {
                    if (!*p) {
                        free(field.data);
                        cmdr_free(&row);
                        cmdr_free(rows);
                        return cmdr_error(error, "%s: row %zu: unclosed quote", name, start);
                    }
                    if (*p == '"' && p[1] != '"') break;
                    if (*p == '"') ++p;
                    if (*p == '\n') ++line;
                    put(&field, p, 1);
                }
                ++p;
                if (*p && *p != delim && *p != '\r' && *p != '\n') {
                    free(field.data);
                    cmdr_free(&row);
                    cmdr_free(rows);
                    return cmdr_error(error, "%s: row %zu: text after a closing quote", name, start);
                }
            } else {
                size_t n = strcspn(p, (char[]){delim, '\r', '\n', '\0'});
                put(&field, p, n);
                p += n;
            }
            end_field(&row, &field);
            if (*p != delim) break;
            ++p;
        }
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;
        ++line;
        cmdr_push(rows, row);
    }
    free(field.data);
    return 0;
}

COMMAND(rows_command)
{
    if (argc != 1 && argc != 2) return cmdr_error(error, "usage: csv.rows TEXT [DELIMITER]");
    char delim;
    if (delimiter("csv.rows", argc, argv, 1, &delim, error)) return 1;
    return parse("csv.rows", cmdr_text_of(&argv[0]), delim, result, error);
}

/* The first row that is not blank names the fields of each later row, which
 * must have as many. Blank rows are skipped, as Python's DictReader does. */
COMMAND(dicts_command)
{
    if (argc != 1 && argc != 2) return cmdr_error(error, "usage: csv.dicts TEXT [DELIMITER]");
    char delim;
    cmdr_value rows;
    if (delimiter("csv.dicts", argc, argv, 1, &delim, error)
        || parse("csv.dicts", cmdr_text_of(&argv[0]), delim, &rows, error))
        return 1;
    const cmdr_value *header = NULL;
    *result = cmdr_list();
    int status = 0;
    for (size_t i = 0; i < cmdr_count(&rows) && !status; ++i) {
        const cmdr_value *row = cmdr_item(&rows, i);
        size_t n = cmdr_count(row);
        if (!n) continue;
        if (!header) {
            header = row;
            for (size_t a = 0; a < n && !status; ++a)
                for (size_t b = 0; b < a && !status; ++b)
                    if (!strcmp(cmdr_text_of(cmdr_item(row, a)), cmdr_text_of(cmdr_item(row, b))))
                        status = cmdr_error(error, "csv.dicts: the header names %s twice",
                                            cmdr_text_of(cmdr_item(row, a)));
            continue;
        }
        if (n != cmdr_count(header)) {
            status = cmdr_error(error, "csv.dicts: row %zu: %zu of the header's %zu fields",
                                i + 1, n, cmdr_count(header));
            break;
        }
        cmdr_value dict = cmdr_dict();
        for (size_t k = 0; k < n; ++k) {
            const char *value = cmdr_text_of(cmdr_item(row, k));
            cmdr_dict_put(&dict, cmdr_text_of(cmdr_item(header, k)), cmdr_text(value, strlen(value)));
        }
        cmdr_push(result, dict);
    }
    cmdr_free(&rows);
    if (status) cmdr_free(result);
    return status;
}

/* Quote a field only when it must be: it holds the delimiter, a quote, or a
 * line break, or it is a row's only field and empty, as Python's
 * QUOTE_MINIMAL does. Each row ends with \n. */
COMMAND(format_command)
{
    if (argc != 1 && argc != 2) return cmdr_error(error, "usage: csv.format ROWS [DELIMITER]");
    char delim;
    if (delimiter("csv.format", argc, argv, 1, &delim, error)) return 1;
    if (cmdr_kind_of(&argv[0]) != CMDR_LIST) return cmdr_error(error, "csv.format: ROWS must be a list of lists");
    buffer out = {0};
    for (size_t i = 0; i < cmdr_count(&argv[0]); ++i) {
        const cmdr_value *row = cmdr_item(&argv[0], i);
        if (cmdr_kind_of(row) != CMDR_LIST) {
            free(out.data);
            return cmdr_error(error, "csv.format: row %zu is not a list", i + 1);
        }
        size_t n = cmdr_count(row);
        for (size_t k = 0; k < n; ++k) {
            const cmdr_value *item = cmdr_item(row, k);
            if (cmdr_kind_of(item) != CMDR_TEXT) {
                free(out.data);
                return cmdr_error(error, "csv.format: row %zu, field %zu is not text", i + 1, k + 1);
            }
            const char *text = cmdr_text_of(item);
            if (k) put(&out, &delim, 1);
            if (!strpbrk(text, (char[]){delim, '"', '\r', '\n', '\0'}) && (*text || n > 1)) {
                put(&out, text, strlen(text));
                continue;
            }
            put(&out, "\"", 1);
            for (const char *p = text; *p; ++p) {
                if (*p == '"') put(&out, "\"\"", 2);
                else put(&out, p, 1);
            }
            put(&out, "\"", 1);
        }
        put(&out, "\n", 1);
    }
    *result = out.data ? cmdr_text_owned(out.data) : cmdr_text("", 0);
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "rows", .usage = "rows TEXT [DELIMITER]", .docstring = "Parse CSV TEXT into a list of rows, each a list of fields",
     .run = rows_command, .text = 1},
    {.name = "dicts", .usage = "dicts TEXT [DELIMITER]", .docstring = "Parse CSV TEXT into a dict per row, keyed by the header",
     .run = dicts_command, .text = 1},
    {.name = "format", .usage = "format ROWS [DELIMITER]", .docstring = "Write a list of rows as CSV text",
     .run = format_command},
};
const cmdr cmdr_ext_csv = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
