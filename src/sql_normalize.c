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
    int in_space = 0;

    for (size_t i = 0; i < sql_len && w < out_size - 1; i++) {
        char c = sql[i];

        /* Replace quoted literals with one placeholder. SQL doubled quotes and
         * backslash escapes are consumed without exposing their contents. */
        if (c == '\'' || c == '"') {
            char quote = c;
            out[w++] = '?';
            in_space = 0;

            for (i++; i < sql_len; i++) {
                if (sql[i] == '\\' && i + 1 < sql_len) {
                    i++;
                    continue;
                }
                if (sql[i] != quote) continue;
                if (i + 1 < sql_len && sql[i + 1] == quote) {
                    i++;
                    continue;
                }
                break;
            }
            continue;
        }

        /* Replace standalone integer, decimal, hexadecimal, and exponent
         * literals. Digits embedded in identifiers remain unchanged. */
        int starts_number = (c >= '0' && c <= '9') &&
            (i == 0 ||
             !((sql[i - 1] >= 'a' && sql[i - 1] <= 'z') ||
               (sql[i - 1] >= 'A' && sql[i - 1] <= 'Z') ||
               (sql[i - 1] >= '0' && sql[i - 1] <= '9') ||
               sql[i - 1] == '_' || sql[i - 1] == '$'));
        if (starts_number) {
            size_t j = i + 1;
            if (c == '0' && j < sql_len &&
                (sql[j] == 'x' || sql[j] == 'X')) {
                j++;
                while (j < sql_len &&
                       ((sql[j] >= '0' && sql[j] <= '9') ||
                        (sql[j] >= 'a' && sql[j] <= 'f') ||
                        (sql[j] >= 'A' && sql[j] <= 'F'))) {
                    j++;
                }
            } else {
                while (j < sql_len && sql[j] >= '0' && sql[j] <= '9') j++;
                if (j < sql_len && sql[j] == '.') {
                    j++;
                    while (j < sql_len && sql[j] >= '0' && sql[j] <= '9') j++;
                }
                if (j < sql_len && (sql[j] == 'e' || sql[j] == 'E')) {
                    size_t exponent = j++;
                    if (j < sql_len && (sql[j] == '+' || sql[j] == '-')) j++;
                    size_t exponent_digits = j;
                    while (j < sql_len && sql[j] >= '0' && sql[j] <= '9') j++;
                    if (j == exponent_digits) j = exponent;
                }
            }

            int ends_number = j == sql_len ||
                !((sql[j] >= 'a' && sql[j] <= 'z') ||
                  (sql[j] >= 'A' && sql[j] <= 'Z') ||
                  (sql[j] >= '0' && sql[j] <= '9') ||
                  sql[j] == '_' || sql[j] == '$');
            if (ends_number) {
                out[w++] = '?';
                i = j - 1;
                in_space = 0;
                continue;
            }
        }

        /* Collapse whitespace */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!in_space && w > 0 && out[w - 1] != ' ') {
                out[w++] = ' ';
                in_space = 1;
            }
            continue;
        }

        in_space = 0;
        out[w++] = c;
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
