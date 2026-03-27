<?php
/**
 * UMC Ultra 在线防伪及绑机状态校验接口
 */
require_once 'config.php';

$input = json_decode(file_get_contents('php://input'), true);
if (!$input || empty($input['key']) || empty($input['machine_id'])) {
    echo json_encode(['status' => 'ok']); // 容错，原样放行
    exit;
}

$key = trim($input['key']);
$machineId = trim($input['machine_id']);

$pdo = getDb();
$stmt = $pdo->prepare("SELECT status, machines_bound FROM licenses WHERE license_key = ?");
$stmt->execute([$key]);
$license = $stmt->fetch();

// 如果密钥不存在/被封禁
if (!$license || $license['status'] !== 'active') {
    echo json_encode(['status' => 'invalid']);
    exit;
}

// 检查当前机器是否还在绑定列表中（防后台解绑）
$machines = json_decode($license['machines_bound'], true) ?? [];
if (!in_array($machineId, $machines)) {
    echo json_encode(['status' => 'invalid']);
    exit;
}

// 正常
echo json_encode(['status' => 'ok']);
