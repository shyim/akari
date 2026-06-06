--TEST--
Symfony EventDispatcher: span name includes event name from dispatch()
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
akari.event_dispatch_min_duration_ms=0
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

namespace App\Event {
    class UserRegistered {}
}

namespace {
    $dispatcher = new \Symfony\Component\EventDispatcher\EventDispatcher();

    // With explicit event name (arg 2)
    $dispatcher->dispatch(new \stdClass(), 'kernel.request');

    // Without event name — falls back to event class name
    $dispatcher->dispatch(new \App\Event\UserRegistered());

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');

    echo "has kernel.request: " . (in_array('event.dispatch kernel.request', $names) ? 'yes' : 'no') . "\n";
    echo "has UserRegistered: " . (in_array('event.dispatch App\Event\UserRegistered', $names) ? 'yes' : 'no') . "\n";
}
?>
--EXPECT--
has kernel.request: yes
has UserRegistered: yes
