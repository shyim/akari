--TEST--
Event dispatch spans honor akari.event_dispatch_min_duration_ms
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
akari.event_dispatch_min_duration_ms=1000
--FILE--
<?php
namespace Symfony\Contracts\EventDispatcher {
    interface EventDispatcherInterface {
        public function dispatch(object $event, ?string $eventName = null): object;
    }
}

namespace Symfony\Component\EventDispatcher {
    use Symfony\Contracts\EventDispatcher\EventDispatcherInterface;

    class EventDispatcher implements EventDispatcherInterface {
        public function dispatch(object $event, ?string $eventName = null): object {
            return $event;
        }
    }
}

namespace {
    $dispatcher = new \Symfony\Component\EventDispatcher\EventDispatcher();
    $dispatcher->dispatch(new \stdClass(), 'kernel.request');

    echo 'spans: ' . \Akari\getSpanCount() . "\n";
}
?>
--EXPECT--
spans: 0
