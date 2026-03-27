<?php
/**
 * UMC Ultra 许可激活 API
 * 
 * 部署: 将此文件放到 https://asmrtop.cn/api/activate.php
 * 配置 .htaccess 重写: RewriteRule ^api/activate$ api/activate.php [L]
 * 
 * 请求: POST JSON {"key": "UMCA-XXXX-XXXX-XXXX", "machine_id": "abc123..."}
 * 响应: {"status": "ok", "token": "...", "expiry": "2027-03-26"}
 *       {"status": "error", "message": "..."}
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

// ====== 配置 ======
define('VERIFY_SALT', 'UMC_ULTRA_2026_ASMRTOP_SEC');   // 必须与 LicenseManager.h 中的 LIC_VERIFY_SALT 一致
define('KEYS_FILE', __DIR__ . '/keys.json');
define('LOG_FILE', __DIR__ . '/activation.log');
define('LICENSE_DURATION_DAYS', 365);  // 激活后有效期 1 年
define('MAX_MACHINES_PER_KEY', 2);     // 每个 key 最多绑定 2 台机器

// ====== 读请求 ======
$input = json_decode(file_get_contents('php://input'), true);
if (!$input || empty($input['key']) || empty($input['machine_id'])) {
    echo json_encode(['status' => 'error', 'message' => 'Missing key or machine_id']);
    exit;
}

$key = trim($input['key']);
$machineId = trim($input['machine_id']);

// ====== 加载密钥库 ======
if (!file_exists(KEYS_FILE)) {
    echo json_encode(['status' => 'error', 'message' => 'Server not configured']);
    exit;
}

$keys = json_decode(file_get_contents(KEYS_FILE), true);
if (!isset($keys[$key])) {
    logAction("REJECT", $key, $machineId, "Invalid key");
    echo json_encode(['status' => 'error', 'message' => '无效的许可密钥']);
    exit;
}

$keyInfo = $keys[$key];

// ====== 检查绑定限制 ======
$machines = $keyInfo['machines'] ?? [];
if (!in_array($machineId, $machines)) {
    if (count($machines) >= MAX_MACHINES_PER_KEY) {
        logAction("REJECT", $key, $machineId, "Max machines reached");
        echo json_encode(['status' => 'error', 'message' => '此密钥已达到最大绑定数量 (' . MAX_MACHINES_PER_KEY . ' 台)']);
        exit;
    }
    $machines[] = $machineId;
}

// ====== 生成 Token ======
$expiry = date('Y-m-d', strtotime('+' . LICENSE_DURATION_DAYS . ' days'));
$token = substr(hash('sha256', $key . '|' . $machineId . '|' . $expiry . '|' . VERIFY_SALT), 0, 32);

// ====== 更新密钥库 ======
$keys[$key] = [
    'machines' => $machines,
    'last_activated' => date('Y-m-d H:i:s'),
    'expiry' => $expiry,
];
file_put_contents(KEYS_FILE, json_encode($keys, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));

logAction("OK", $key, $machineId, "Expiry: $expiry");

echo json_encode([
    'status' => 'ok',
    'token' => $token,
    'expiry' => $expiry,
]);

// ====== 日志 ======
function logAction($status, $key, $machine, $detail) {
    $line = date('Y-m-d H:i:s') . " | $status | Key=$key | Machine=$machine | $detail\n";
    @file_put_contents(LOG_FILE, $line, FILE_APPEND | LOCK_EX);
}
