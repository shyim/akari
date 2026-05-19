PHP_ARG_ENABLE([akari],
  [whether to enable akari support],
  [AS_HELP_STRING([--enable-akari],
    [Enable akari support])],
  [no])

if test "$PHP_AKARI" != "no"; then
  AC_DEFINE(HAVE_AKARI, 1, [Whether you have akari])

  PHP_ADD_INCLUDE($ext_srcdir/include)

  dnl Platform-specific timer backend for sampling mode
  profiler_timer_sources=""

  AC_CHECK_DECL(SIGEV_THREAD_ID, [
    AC_DEFINE(HAVE_SIGEV_THREAD_ID, 1, [Whether SIGEV_THREAD_ID is available])
    AC_CHECK_LIB(rt, timer_create, [
      PHP_ADD_LIBRARY(rt,, AKARI_SHARED_LIBADD)
    ])
    AC_CHECK_DECL(gettid, [
      AC_DEFINE(HAVE_GETTID, 1, [Whether gettid() is available])
    ], [], [
      #define _GNU_SOURCE
      #include <unistd.h>
    ])
    profiler_timer_sources="src/sampler_timer_posix.c"
  ], [
    AC_SEARCH_LIBS(kevent, [kqueue], [
      AC_DEFINE(HAVE_KQUEUE, 1, [Whether kqueue is available])
      profiler_timer_sources="src/sampler_timer_kqueue.c"
    ], [
      AC_MSG_ERROR([akari sampling mode requires timer_create (Linux) or kqueue (macOS/BSD)])
    ])
  ], [
    #include <signal.h>
    #include <time.h>
  ])

  PHP_SUBST(AKARI_SHARED_LIBADD)

  PHP_NEW_EXTENSION(akari,
    src/php_akari.c src/profiler.c src/profiler_span.c src/hook_registry.c src/observer.c src/hook_pdo.c src/hook_curl.c src/hook_redis.c src/hook_amqp.c src/hook_mysqli.c src/hook_memcached.c src/hook_io.c src/hook_framework.c src/hook_twig.c src/hook_doctrine.c src/hook_symfony.c src/hook_elasticsearch.c src/hook_predis.c src/hook_laravel.c src/hook_shopware.c src/hook_error.c src/hook_sampler.c src/hook_root_span.c src/hook_oci8.c src/hook_grpc.c src/hook_rdkafka.c src/hook_graphql.c src/hook_soap.c src/hook_pheanstalk.c src/hook_php_streams.c src/hook_pcre.c src/hook_engine.c src/sql_normalize.c src/otlp_export.c src/udp_export.c $profiler_timer_sources,
    $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 -std=c11 -fvisibility=hidden)
fi
