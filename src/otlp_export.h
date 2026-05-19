#ifndef OTLP_EXPORT_H
#define OTLP_EXPORT_H

#include "profiler.h"

/* Serialize spans to OTLP JSON for introspection (getSpansJson). Caller must free(). */
char *otlp_serialize_spans(profiler_state_t *state, const char *service_name, size_t *out_len);

#endif /* OTLP_EXPORT_H */
