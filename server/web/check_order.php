<?php
/**
 * Frontend AJAX Polling for Z-Pay order status
 */
require_once 'config.php';

$out_trade_no = $_GET['order'] ?? '';
if (!$out_trade_no) {
    echo json_encode(['status' => 'error', 'msg' => 'Missing order ID']);
    exit;
}

$pdo = getDb();
$stmt = $pdo->prepare("SELECT status, license_key FROM orders WHERE out_trade_no = ?");
$stmt->execute([$out_trade_no]);
$order = $stmt->fetch();

if ($order && $order['status'] === 'paid') {
    echo json_encode([
        'status' => 'success',
        'license_key' => $order['license_key']
    ]);
} else {
    echo json_encode([
        'status' => 'pending'
    ]);
}
