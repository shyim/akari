<?php
/**
 * Combined demo — shows all instrumentation features in one trace.
 */

class UserService {
    private PDO $db;

    public function __construct() {
        $this->db = new PDO('sqlite:/tmp/demo.db');
        $this->db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $this->db->exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)");
    }

    public function findAll(): array {
        $stmt = $this->db->prepare("SELECT * FROM users ORDER BY name");
        $stmt->execute();
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    }

    public function create(string $name, string $email): void {
        $stmt = $this->db->prepare("INSERT OR REPLACE INTO users (name, email) VALUES (?, ?)");
        $stmt->execute([$name, $email]);
    }
}

class ApiClient {
    public function fetchUserAvatar(string $email): string {
        $hash = md5(strtolower(trim($email)));
        $url = "https://gravatar.com/avatar/{$hash}?d=identicon&s=32";

        $ch = curl_init($url);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($ch, CURLOPT_TIMEOUT, 5);
        curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
        curl_exec($ch);
        $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);

        return $code == 200 ? $url : '';
    }
}

class RequestHandler {
    public function handle(): string {
        $userService = new UserService();
        $apiClient = new ApiClient();

        // Seed some data
        $userService->create('Alice', 'alice@example.com');
        $userService->create('Bob', 'bob@example.com');

        // Fetch users from DB
        $users = $userService->findAll();

        // Enrich with avatar URLs (outgoing HTTP)
        $enriched = array_map(function($user) use ($apiClient) {
            $user['avatar'] = $apiClient->fetchUserAvatar($user['email']);
            return $user;
        }, $users);

        return $this->render($enriched);
    }

    private function render(array $users): string {
        $html = "<h2>Complex Trace Demo</h2>";
        $html .= "<p>This page generates a trace with:</p><ul>";
        $html .= "<li>SERVER root span (the HTTP request)</li>";
        $html .= "<li>INTERNAL spans (PHP functions/methods)</li>";
        $html .= "<li>CLIENT spans (PDO queries with db.statement)</li>";
        $html .= "<li>CLIENT spans (curl requests with traceparent injection)</li>";
        $html .= "</ul>";
        $html .= "<table border='1' cellpadding='5'>";
        $html .= "<tr><th>Name</th><th>Email</th><th>Avatar</th></tr>";
        foreach ($users as $user) {
            $avatar = $user['avatar'] ? "<img src='{$user['avatar']}' width='32'>" : 'N/A';
            $html .= "<tr><td>{$user['name']}</td><td>{$user['email']}</td><td>{$avatar}</td></tr>";
        }
        $html .= "</table>";
        $html .= "<p><em>Open <a href='http://localhost:16686'>Jaeger</a>, select service 'demo-php-app', and click 'Find Traces'</em></p>";
        return $html;
    }
}

$handler = new RequestHandler();
echo $handler->handle();
