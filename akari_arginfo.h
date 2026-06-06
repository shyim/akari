/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: ace0fa21fc977e7d8608ca849a682dd081dcc000 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_enable, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_Akari_disable arginfo_Akari_enable

#if defined(AKARI_DEBUG_INTROSPECTION)
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_getSpanCount, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_Akari_getFrameCount arginfo_Akari_getSpanCount

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_Akari_getSpansJson, 0, 0, MAY_BE_STRING|MAY_BE_FALSE)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_getTags, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_Akari_getLogsJson arginfo_Akari_getSpansJson

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_isProfiling, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()
#endif

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_setTransactionName, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_getTransactionName, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

#define arginfo_Akari_setServiceName arginfo_Akari_setTransactionName

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_addTag, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_removeTag, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_Akari_setCustomVariable arginfo_Akari_addTag

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_Akari_createSpan, 0, 1, MAY_BE_STRING|MAY_BE_FALSE)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_logException, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, exception, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_log, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, level, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, message, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, context, IS_ARRAY, 1, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_generateDistributedTracingHeaders, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Akari_markAsWebTransaction, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

#define arginfo_Akari_markAsCliTransaction arginfo_Akari_markAsWebTransaction

ZEND_FUNCTION(Akari_enable);
ZEND_FUNCTION(Akari_disable);
#if defined(AKARI_DEBUG_INTROSPECTION)
ZEND_FUNCTION(Akari_getSpanCount);
ZEND_FUNCTION(Akari_getFrameCount);
ZEND_FUNCTION(Akari_getSpansJson);
ZEND_FUNCTION(Akari_getTags);
ZEND_FUNCTION(Akari_getLogsJson);
ZEND_FUNCTION(Akari_isProfiling);
#endif
ZEND_FUNCTION(Akari_setTransactionName);
ZEND_FUNCTION(Akari_getTransactionName);
ZEND_FUNCTION(Akari_setServiceName);
ZEND_FUNCTION(Akari_addTag);
ZEND_FUNCTION(Akari_removeTag);
ZEND_FUNCTION(Akari_setCustomVariable);
ZEND_FUNCTION(Akari_createSpan);
ZEND_FUNCTION(Akari_logException);
ZEND_FUNCTION(Akari_log);
ZEND_FUNCTION(Akari_generateDistributedTracingHeaders);
ZEND_FUNCTION(Akari_markAsWebTransaction);
ZEND_FUNCTION(Akari_markAsCliTransaction);

static const zend_function_entry ext_functions[] = {
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "enable"), zif_Akari_enable, arginfo_Akari_enable, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "enable"), zif_Akari_enable, arginfo_Akari_enable, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "disable"), zif_Akari_disable, arginfo_Akari_disable, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "disable"), zif_Akari_disable, arginfo_Akari_disable, 0)
#endif
#if defined(AKARI_DEBUG_INTROSPECTION)
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getSpanCount"), zif_Akari_getSpanCount, arginfo_Akari_getSpanCount, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getSpanCount"), zif_Akari_getSpanCount, arginfo_Akari_getSpanCount, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getFrameCount"), zif_Akari_getFrameCount, arginfo_Akari_getFrameCount, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getFrameCount"), zif_Akari_getFrameCount, arginfo_Akari_getFrameCount, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getSpansJson"), zif_Akari_getSpansJson, arginfo_Akari_getSpansJson, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getSpansJson"), zif_Akari_getSpansJson, arginfo_Akari_getSpansJson, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getTags"), zif_Akari_getTags, arginfo_Akari_getTags, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getTags"), zif_Akari_getTags, arginfo_Akari_getTags, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getLogsJson"), zif_Akari_getLogsJson, arginfo_Akari_getLogsJson, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getLogsJson"), zif_Akari_getLogsJson, arginfo_Akari_getLogsJson, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "isProfiling"), zif_Akari_isProfiling, arginfo_Akari_isProfiling, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "isProfiling"), zif_Akari_isProfiling, arginfo_Akari_isProfiling, 0)
#endif
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setTransactionName"), zif_Akari_setTransactionName, arginfo_Akari_setTransactionName, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setTransactionName"), zif_Akari_setTransactionName, arginfo_Akari_setTransactionName, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getTransactionName"), zif_Akari_getTransactionName, arginfo_Akari_getTransactionName, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "getTransactionName"), zif_Akari_getTransactionName, arginfo_Akari_getTransactionName, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setServiceName"), zif_Akari_setServiceName, arginfo_Akari_setServiceName, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setServiceName"), zif_Akari_setServiceName, arginfo_Akari_setServiceName, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "addTag"), zif_Akari_addTag, arginfo_Akari_addTag, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "addTag"), zif_Akari_addTag, arginfo_Akari_addTag, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "removeTag"), zif_Akari_removeTag, arginfo_Akari_removeTag, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "removeTag"), zif_Akari_removeTag, arginfo_Akari_removeTag, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setCustomVariable"), zif_Akari_setCustomVariable, arginfo_Akari_setCustomVariable, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "setCustomVariable"), zif_Akari_setCustomVariable, arginfo_Akari_setCustomVariable, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "createSpan"), zif_Akari_createSpan, arginfo_Akari_createSpan, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "createSpan"), zif_Akari_createSpan, arginfo_Akari_createSpan, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "logException"), zif_Akari_logException, arginfo_Akari_logException, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "logException"), zif_Akari_logException, arginfo_Akari_logException, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "log"), zif_Akari_log, arginfo_Akari_log, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "log"), zif_Akari_log, arginfo_Akari_log, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "generateDistributedTracingHeaders"), zif_Akari_generateDistributedTracingHeaders, arginfo_Akari_generateDistributedTracingHeaders, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "generateDistributedTracingHeaders"), zif_Akari_generateDistributedTracingHeaders, arginfo_Akari_generateDistributedTracingHeaders, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "markAsWebTransaction"), zif_Akari_markAsWebTransaction, arginfo_Akari_markAsWebTransaction, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "markAsWebTransaction"), zif_Akari_markAsWebTransaction, arginfo_Akari_markAsWebTransaction, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "markAsCliTransaction"), zif_Akari_markAsCliTransaction, arginfo_Akari_markAsCliTransaction, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Akari", "markAsCliTransaction"), zif_Akari_markAsCliTransaction, arginfo_Akari_markAsCliTransaction, 0)
#endif
	ZEND_FE_END
};
