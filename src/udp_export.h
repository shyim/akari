#ifndef UDP_EXPORT_H
#define UDP_EXPORT_H

#include "profiler.h"

int udp_export_init(const char *host, int port);
void udp_export_shutdown(void);
void udp_export_spans(profiler_state_t *state, const char *service_name);
void udp_export_logs(profiler_state_t *state, const char *service_name);

#endif /* UDP_EXPORT_H */
