<?php
/**
 * @generate-function-entries
 * @generate-legacy-arginfo 80200
 */

namespace Akari;

function enable(): bool {}

function disable(): bool {}

#ifdef AKARI_DEBUG_INTROSPECTION
function getSpanCount(): int {}

function getFrameCount(): int {}

function getSpansJson(): string|false {}

function getTags(): array {}

function getLogsJson(): string|false {}

function isProfiling(): bool {}
#endif

function setTransactionName(string $name): void {}

function getTransactionName(): ?string {}

function setServiceName(string $name): void {}

function addTag(string $key, string $value): void {}

function removeTag(string $key): void {}

function setCustomVariable(string $key, string $value): void {}

function createSpan(string $name): string|false {}

function logException(\Throwable $exception): void {}

function log(string $level, string $message, ?array $context = []): void {}

function generateDistributedTracingHeaders(): array {}

function markAsWebTransaction(): void {}

function markAsCliTransaction(): void {}
