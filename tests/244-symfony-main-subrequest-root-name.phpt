--TEST--
Symfony: only MAIN_REQUEST updates root name and subrequests get named child spans
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
namespace Symfony\Component\HttpFoundation {
    class ParameterBag {
        private array $params = [];
        public function set(string $key, $value): void { $this->params[$key] = $value; }
        public function get(string $key, $default = null) { return $this->params[$key] ?? $default; }
    }
    class Request {
        public ParameterBag $attributes;
        public function __construct() {
            $this->attributes = new ParameterBag();
        }
    }
    class Response {}
}

namespace Symfony\Component\HttpKernel {
    use Symfony\Component\HttpFoundation\Request;
    use Symfony\Component\HttpFoundation\Response;

    interface HttpKernelInterface {
        public const MAIN_REQUEST = 1;
        public const SUB_REQUEST = 2;
        public function handle(Request $request, int $type = self::MAIN_REQUEST): Response;
    }

    class HttpKernel implements HttpKernelInterface {
        public function handle(Request $request, int $type = self::MAIN_REQUEST): Response {
            return new Response();
        }
    }
}

namespace {
    $main = new \Symfony\Component\HttpFoundation\Request();
    $main->attributes->set('_controller', 'App\\Controller\\HomeController::index');
    $main->attributes->set('_route', 'frontend.home.page');

    $sub = new \Symfony\Component\HttpFoundation\Request();
    $sub->attributes->set('_controller', 'App\\Controller\\FooterController::index');
    $sub->attributes->set('_route', 'frontend.footer');

    $late = new \Symfony\Component\HttpFoundation\Request();
    $late->attributes->set('_controller', 'App\\Controller\\SidebarController::index');
    $late->attributes->set('_route', 'frontend.sidebar');

    $kernel = new \Symfony\Component\HttpKernel\HttpKernel();
    $kernel->handle($main, \Symfony\Component\HttpKernel\HttpKernelInterface::MAIN_REQUEST);
    $kernel->handle($sub, \Symfony\Component\HttpKernel\HttpKernelInterface::SUB_REQUEST);
    $kernel->handle($late);

    echo 'transaction: ' . \Akari\getTransactionName() . "\n";

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');

    echo 'has subrequest span: '
        . (in_array('symfony.subrequest App\\Controller\\FooterController::index', $names, true) ? 'yes' : 'no')
        . "\n";
    echo 'has late target span: '
        . (in_array('symfony.subrequest App\\Controller\\SidebarController::index', $names, true) ? 'yes' : 'no')
        . "\n";
}
?>
--EXPECT--
transaction: HTTP App\Controller\HomeController::index
has subrequest span: yes
has late target span: yes
