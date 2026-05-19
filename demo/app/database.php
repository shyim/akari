<?php
/**
 * Database tracing demo with PDO/SQLite.
 * Shows: db.system, db.statement, PDO::exec, PDOStatement::execute
 */

$db = new PDO('sqlite:/tmp/demo.db');
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

// Schema
$db->exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)");

// Insert
$db->exec("DELETE FROM users");
$db->exec("INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
$db->exec("INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')");
$db->exec("INSERT INTO users (name, email) VALUES ('Charlie', 'charlie@example.com')");

// Prepared statement
$stmt = $db->prepare("SELECT * FROM users WHERE name LIKE ?");
$stmt->execute(['%li%']);
$results = $stmt->fetchAll(PDO::FETCH_ASSOC);

// Aggregation
$count = $db->query("SELECT COUNT(*) as total FROM users")->fetchColumn();

echo "<h2>Database Tracing Demo</h2>";
echo "<p>Total users: {$count}</p>";
echo "<table border='1' cellpadding='5'>";
echo "<tr><th>ID</th><th>Name</th><th>Email</th></tr>";
foreach ($results as $row) {
    echo "<tr><td>{$row['id']}</td><td>{$row['name']}</td><td>{$row['email']}</td></tr>";
}
echo "</table>";
echo "<p><em>Check Jaeger — you'll see CLIENT spans with db.system=sqlite and db.statement</em></p>";
