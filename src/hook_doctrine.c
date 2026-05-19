#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Doctrine ORM instrumentation — userland hooks.
 *
 * Only hooks the high-level ORM operations that wrap multiple SQL calls.
 * Individual SQL execution is already captured by PDO hooks with full
 * db.statement / db.system attributes — no need to duplicate at DBAL level.
 *
 * Hooks:
 *   - EntityManager::flush() — computes changesets + executes all pending SQL
 */

/* ── Registration ── */

void hook_doctrine_register(hook_registry_t *reg)
{
    /* EntityManager::flush — the main ORM write boundary.
     * Triggers UnitOfWork to compute changesets and run SQL.
     * Individual queries inside flush are captured by PDO hooks. */
    hook_register_method(reg,
        "Doctrine\\ORM\\EntityManagerInterface", "flush",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
}
