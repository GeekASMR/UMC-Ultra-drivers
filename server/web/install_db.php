<?php
require_once 'config.php';

try {
    $pdo = getDb();
    
    // Read the SQL commands from db.sql
    $sql = file_get_contents('db.sql');
    if (!$sql) {
        die("Error: db.sql not found or empty.");
    }
    
    // Execute multiple statements
    $pdo->exec($sql);
    echo "Database tables created successfully!";
    
    // Self-delete for security
    unlink(__FILE__);
} catch (Exception $e) {
    echo "Database initialization failed: " . $e->getMessage();
}
