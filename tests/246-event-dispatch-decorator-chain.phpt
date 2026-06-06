--TEST--
Symfony EventDispatcher: decorator chains collapse to one logical event span
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

namespace App {
    use Symfony\Contracts\EventDispatcher\EventDispatcherInterface;

    class MainEvent {}
    class NestedEvent {}

    class OuterDispatcher implements EventDispatcherInterface {
        public function __construct(private EventDispatcherInterface $inner) {}

        public function dispatch(object $event, ?string $eventName = null): object {
            return $this->inner->dispatch($event, $eventName);
        }
    }

    class InnerDispatcher implements EventDispatcherInterface {
        public function __construct(private EventDispatcherInterface $inner) {}

        public function dispatch(object $event, ?string $eventName = null): object {
            return $this->inner->dispatch($event, $eventName);
        }
    }

    class EventDispatcher implements EventDispatcherInterface {
        private bool $nested = false;

        public function dispatch(object $event, ?string $eventName = null): object {
            if (!$this->nested) {
                $this->nested = true;
                $this->dispatch(new NestedEvent(), 'nested.event');
            }

            return $event;
        }
    }
}

namespace {
    $dispatcher = new \App\OuterDispatcher(
        new \App\InnerDispatcher(
            new \App\EventDispatcher()
        )
    );
    $dispatcher->dispatch(new \App\MainEvent(), 'kernel.controller');

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');

    echo 'kernel.controller spans: '
        . count(array_filter($names, static fn ($name) => $name === 'event.dispatch kernel.controller'))
        . "\n";
    echo 'nested.event spans: '
        . count(array_filter($names, static fn ($name) => $name === 'event.dispatch nested.event'))
        . "\n";
    echo 'outer method span: '
        . (in_array('App\\OuterDispatcher::dispatch', $names, true) ? 'yes' : 'no')
        . "\n";
}
?>
--EXPECT--
kernel.controller spans: 1
nested.event spans: 1
outer method span: no
