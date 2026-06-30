PHP_ARG_ENABLE([akari],
  [whether to enable akari support],
  [AS_HELP_STRING([--enable-akari],
    [Enable akari support])],
  [no])

PHP_ARG_ENABLE([akari-debug],
  [whether to enable akari debug introspection functions],
  [AS_HELP_STRING([--enable-akari-debug],
    [Enable akari debug introspection functions (getSpansJson, getLogsJson, getTags, getSpanCount, getFrameCount, isProfiling). For testing only; not for production.])],
  [no], [no])

if test "$PHP_AKARI" != "no"; then
  AC_CHECK_HEADERS([curl/curl.h], [], [
    AC_MSG_ERROR([curl headers not found. Install libcurl-dev or curl-devel.])
  ])

  AC_DEFINE(HAVE_AKARI, 1, [Whether you have akari])

  AKARI_CFLAGS="-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 -std=c11 -fvisibility=hidden"
  if test "$PHP_AKARI_DEBUG" != "no"; then
    AKARI_CFLAGS="$AKARI_CFLAGS -DAKARI_DEBUG_INTROSPECTION=1"
  fi

  PHP_ADD_INCLUDE($ext_srcdir/include)

  PHP_SUBST(AKARI_SHARED_LIBADD)

  PHP_NEW_EXTENSION(akari,
    src/php_akari.c src/profiler.c src/profiler_span.c src/hook_registry.c src/observer.c src/hook_attribute.c src/hook_pdo.c src/hook_sqlite3.c src/hook_curl.c src/hook_redis.c src/hook_amqp.c src/hook_mysqli.c src/hook_memcached.c src/hook_io.c src/hook_framework.c src/hook_twig.c src/hook_doctrine.c src/hook_symfony.c src/hook_elasticsearch.c src/hook_predis.c src/hook_laravel.c src/hook_shopware.c src/hook_error.c src/hook_root_span.c src/hook_oci8.c src/hook_grpc.c src/hook_rdkafka.c src/hook_graphql.c src/hook_soap.c src/hook_pheanstalk.c src/hook_php_streams.c src/hook_pcre.c src/hook_engine.c src/sql_normalize.c src/otlp_export.c src/udp_export.c,
    $ext_shared,, $AKARI_CFLAGS)
fi
