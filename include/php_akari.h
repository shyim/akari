#ifndef PHP_AKARI_H
#define PHP_AKARI_H

#include "main/php.h"

#define PHP_AKARI_VERSION "0.1.0"
#define PHP_AKARI_EXTNAME "akari"
#define PHP_AKARI_NS "Akari"

extern zend_module_entry akari_module_entry;
#define phpext_akari_ptr &akari_module_entry

#endif /* PHP_AKARI_H */
