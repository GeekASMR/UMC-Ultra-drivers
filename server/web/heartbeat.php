<?php
require_once 'config.php';

$input = json_decode(file_get_contents('php://input'), true);
if (!$input || empty($input['machine_id'])) {
    echo json_encode(['status' => 'error']);
    exit;
}

$machineId = trim($input['machine_id']);
$status = trim($input['status'] ?? 'trial'); // 'active' or 'trial'
$rem = (int)($input['rem'] ?? 0); // remaining minutes

$pdo = getDb();
try {
    $pdo->exec("CREATE TABLE IF NOT EXISTS machines (
        machine_id VARCHAR(128) PRIMARY KEY,
        status VARCHAR(32),
        rem_minutes INT,
        last_seen DATETIME,
        reset_flag INT DEFAULT 0
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
} catch(Exception $e) {}

$upd = $pdo->prepare("INSERT INTO machines (machine_id, status, rem_minutes, last_seen, reset_flag) 
    VALUES (?, ?, ?, NOW(), 0) 
    ON DUPLICATE KEY UPDATE status=?, rem_minutes=?, last_seen=NOW()");
$upd->execute([$machineId, $status, $rem, $status, $rem]);

// Check if admin pushed a reset flag
$chk = $pdo->prepare("SELECT reset_flag FROM machines WHERE machine_id = ?");
$chk->execute([$machineId]);
$resetFlag = (int)$chk->fetchColumn();

if ($resetFlag == 1) {
    // Ack reset
    $pdo->prepare("UPDATE machines SET reset_flag = 0 WHERE machine_id = ?")->execute([$machineId]);
    echo json_encode(['command' => 'reset_trial']);
} else {
    echo json_encode(['command' => 'none']);
}
