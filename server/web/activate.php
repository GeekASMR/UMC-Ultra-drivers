<?php
/**
 * UMC Ultra 激活动力终点 (ASIO 客户端通信接口)
 * 依赖于 MySQL
 */
require_once 'config.php';

$input = json_decode(file_get_contents('php://input'), true);
if (!$input || empty($input['key']) || empty($input['machine_id'])) {
    echo json_encode(['status' => 'error', 'message' => 'Missing key or machine_id']);
    exit;
}

$key = trim($input['key']);
$machineId = trim($input['machine_id']);

$pdo = getDb();
$stmt = $pdo->prepare("SELECT * FROM licenses WHERE license_key = ?");
$stmt->execute([$key]);
$license = $stmt->fetch();

if (!$license) {
    echo json_encode(['status' => 'error', 'message' => '无效的许可密钥']);
    exit;
}

if ($license['status'] !== 'active') {
    echo json_encode(['status' => 'error', 'message' => '该激活码已被封禁或无效']);
    exit;
}

// Check machines json
$machines = json_decode($license['machines_bound'], true) ?? [];
$maxMachines = (int)$license['max_machines'];

if (!in_array($machineId, $machines)) {
    if (count($machines) >= $maxMachines) {
        echo json_encode(['status' => 'error', 'message' => "此密钥已达到最大绑定数量 ({$maxMachines} 台)"]);
        exit;
    }
    // Bind new machine
    $machines[] = $machineId;
}

// 设定永久过期时间 (100年)，或者读取表里面的 expiry_date
$expiry = $license['expiry_date'];
if (!$expiry) {
    // 第一次激活时设定100年后过期 (永久)
    $expiry = date('Y-m-d', strtotime('+36500 days'));
}

// 生成令牌 Token，防止客户端造假
$token = substr(hash('sha256', $key . '|' . $machineId . '|' . $expiry . '|' . VERIFY_SALT), 0, 32);

// 更新入库
$upd = $pdo->prepare("UPDATE licenses SET machines_bound = ?, expiry_date = ?, last_activated = NOW() WHERE id = ?");
$upd->execute([json_encode($machines), $expiry, $license['id']]);

echo json_encode([
    'status' => 'ok',
    'token' => $token,
    'expiry' => $expiry,
]);
