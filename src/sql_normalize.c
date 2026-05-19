#include "profiler_internal.h"

/*
 * SQL normalization — truncates and normalizes SQL queries for display.
 *
 * Normalization steps:
 *   1. Truncate to DB_STATEMENT_MAX (default 2048 chars)
 *   2. Replace quoted strings with ?
 *   3. Replace numeric literals with ?
 *   4. Collapse whitespace
 *
 * This produces a "fingerprint" of the query suitable for grouping
 * similar queries together in dashboards.
 */

/* ── Public API ── */

void profiler_sql_normalize(const char *sql, size_t sql_len,
                              char *out, size_t out_size,
                              size_t *out_len)
{
    if (!sql || sql_len == 0 || !out || out_size == 0) {
        if (out_len) *out_len = 0;
        if (out && out_size > 0) out[0] = '\0';
        return;
    }

    /* Truncate to max */
    if (sql_len >= out_size) sql_len = out_size - 1;

    size_t w = 0;
    int in_string = 0;   /* inside '...' */
    int in_dquote = 0;   /* inside "..." */
    int in_space = 0;
    char prev = '\0';

    for (size_t i = 0; i < sql_len && w < out_size - 1; i++) {
        char c = sql[i];

        /* Handle string literals */
        if (c == '\'' && !in_dquote) {
            if (!in_string) {
                in_string = 1;
                out[w++] = '?';
                continue;
            } else if (i + 1 < sql_len && sql[i + 1] == '\'') {
                /* Escaped quote: skip it */
                i++;
                continue;
            } else {
                in_string = 0;
                continue;
            }
        }

        if (c == '"' && !in_string) {
            if (!in_dquote) {
                in_dquote = 1;
                out[w++] = '?';
                continue;
            } else {
                in_dquote = 0;
                continue;
            }
        }

        /* Skip content inside strings */
        if (in_string || in_dquote) {
            continue;
        }

        /* Replace numeric literals (standalone numbers after =/< />/etc) */
        if ((c >= '0' && c <= '9') && (prev == '=' || prev == '<' || prev == '>'
            || prev == ' ' || prev == ',' || prev == '(')) {
            /* Check if followed by more digits or whitespace/punctuation */
            size_t j = i + 1;
            while (j < sql_len && sql[j] >= '0' && sql[j] <= '9') j++;
            if (j < sql_len && (sql[j] == ' ' || sql[j] == ',' || sql[j] == ')'
                || sql[j] == ';' || sql[j] == '\n')) {
                if (w > 0 && out[w - 1] != ' ') out[w++] = ' ';
                out[w++] = '?';
                i = j; /* skip the number */
                prev = '?';
                continue;
            }
        }

        /* Collapse whitespace */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!in_space && w > 0 && out[w - 1] != ' ') {
                out[w++] = ' ';
                in_space = 1;
            }
            prev = c;
            continue;
        }

        in_space = 0;
        out[w++] = c;
        prev = c;
    }

    /* Trim trailing space */
    while (w > 0 && out[w - 1] == ' ') w--;

    out[w] = '\0';
    if (out_len) *out_len = w;
}

/*
 * SQL truncation only — keeps original query but truncates to max length.
 * Adds "..." at the end if truncated.
 */

void profiler_sql_truncate(const char *sql, size_t sql_len,
                             char *out, size_t out_size,
                             size_t *out_len)
{
    if (!sql || sql_len == 0 || !out || out_size == 0) {
        if (out_len) *out_len = 0;
        if (out && out_size > 0) out[0] = '\0';
        return;
    }

    if (sql_len + 3 < out_size) {
        memcpy(out, sql, sql_len);
        out[sql_len] = '\0';
        if (out_len) *out_len = sql_len;
    } else {
        size_t copy = out_size - 4;
        if (copy > sql_len) copy = sql_len;
        memcpy(out, sql, copy);
        out[copy] = '.';
        out[copy + 1] = '.';
        out[copy + 2] = '.';
        out[copy + 3] = '\0';
        if (out_len) *out_len = copy + 3;
    }
}
