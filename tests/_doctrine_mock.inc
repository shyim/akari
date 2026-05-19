<?php
/*
 * Shared fixture: a minimal mock of Doctrine's EntityManagerInterface so the
 * userland Doctrine hook (EntityManagerInterface::flush) fires. This gives
 * tests a registered userland span without requiring the real ORM, and lets
 * them nest internal hooks (PDO, I/O) underneath a userland span — the same
 * shape an APM trace has in production (ORM flush → SQL).
 */

namespace Doctrine\ORM {
    interface EntityManagerInterface
    {
        public function flush();
    }
}
