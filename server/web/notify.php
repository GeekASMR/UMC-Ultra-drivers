<?php
/**
 * Z-Pay 异步通知回调
 * 接收到支付成功后，自动下发 UMCA 激活动态码
 */
require_once 'config.php';

// 验证请求是否为合法的 POST / GET
$data = $_GET; // Z-Pay 通常通过 GET 带有回调参数传递
if (empty($data)) {
    die("error");
}

$sign = $data['sign'] ?? '';
$sign_type = $data['sign_type'] ?? '';

// 剔除 sign 和 sign_type 参与签名验证
unset($data['sign'], $data['sign_type']);

// 按字母顺序排序
ksort($data);

// 拼装字符串进行 MD5 签名校验
$str = '';
foreach ($data as $k => $v) {
    if ($v === '' || $v === null) continue;
    $str .= $k . '=' . $v . '&';
}
$str = rtrim($str, '&');
$mySign = md5($str . ZPAY_KEY);

if ($mySign !== $sign) {
    // 签名失败
    file_put_contents('notify.log', date('Y-m-d H:i:s') . " | SIGN ERROR | " . json_encode($_GET) . "\n", FILE_APPEND);
    die("fail");
}

// 签名成功，开始处理业务逻辑
$out_trade_no = $data['out_trade_no'] ?? '';
$trade_no = $data['trade_no'] ?? '';
$trade_status = $data['trade_status'] ?? ''; // z-pay 会带TRADE_SUCCESS吗? 通常回调请求即代表成功
$money = $data['money'] ?? '';

$pdo = getDb();
$stmt = $pdo->prepare("SELECT * FROM orders WHERE out_trade_no = ?");
$stmt->execute([$out_trade_no]);
$order = $stmt->fetch();

if (!$order) {
    die("fail"); // 订单不存在
}

// 如果订单已经是 paid 状态，直接返回 success 防止并发重复回调
if ($order['status'] === 'paid') {
    die("success");
}

// 金额校验 (Z-Pay 必须对账，防止通过 0.01 漏洞购买)
// floating point in DB vs notification API 
if (abs(floatval($order['money']) - floatval($money)) > 0.01) {
    die("fail"); 
}

// ========================
// 支付成功：发卡逻辑
// ========================
// 生成 UMCA-开头的四段式序列号
$licenseKey = 'UMCA-' 
    . strtoupper(bin2hex(random_bytes(2))) . '-'
    . strtoupper(bin2hex(random_bytes(2))) . '-'
    . strtoupper(bin2hex(random_bytes(2)));

$pdo->beginTransaction();
try {
    // 1. 插入卡密池 licenses 表
    $ins = $pdo->prepare("INSERT INTO licenses (license_key, max_machines, machines_bound, order_no) VALUES (?, ?, '[]', ?)");
    $ins->execute([$licenseKey, LICENSE_MAX_MACHINES, $out_trade_no]);
    
    // 2. 更新订单状态为已支付
    $upd = $pdo->prepare("UPDATE orders SET status = 'paid', trade_no = ?, paid_at = NOW(), license_key = ? WHERE id = ?");
    $upd->execute([$trade_no, $licenseKey, $order['id']]);
    
    $pdo->commit();
    echo "success"; // 务必按 Z-Pay 要求返回小写 success
    file_put_contents('notify.log', date('Y-m-d H:i:s') . " | SUCCESS | $out_trade_no -> $licenseKey\n", FILE_APPEND);
} catch (Exception $e) {
    $pdo->rollBack();
    file_put_contents('notify.log', date('Y-m-d H:i:s') . " | DB ERROR | " . $e->getMessage() . "\n", FILE_APPEND);
    die("fail");
}
