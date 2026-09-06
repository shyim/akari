#include "profiler_internal.h"
#include "hook_registry.h"

void hook_php_streams_register(hook_registry_t *reg)
{
    /* Scheme-aware stream functions are registered once by hook_io_register().
     * Keeping ownership in one module avoids duplicate first-write-wins hash
     * entries while allowing each call to select HTTP or filesystem semantics. */
    (void)reg;
}
