<?php
/**
 * UMC Ultra 激活动态发卡支付发起端
 */
require_once 'config.php';

// 生成订单号
$out_trade_no = date('YmdHis') . rand(1000, 9999);
$money = LICENSE_PRICE;
$name = "UMC Ultra 音频驱动高级授权";

// 支持的支付类型：alipay, wxpay, qqpay
$type = $_POST['type'] ?? 'alipay';

// --- 保存到数据库 ---
$pdo = getDb();
$stmt = $pdo->prepare("INSERT INTO orders (out_trade_no, money, pay_type) VALUES (?, ?, ?)");
$stmt->execute([$out_trade_no, $money, $type]);

// --- 构造 Z-Pay 签名 ---
// Z-Pay 签名规则: 将 a-z 按照 ASCII 从小到大排序 (去除 null/空/sign/sign_type), 拼接成 URL 参数再 + KEY, 进行 MD5
$params = [
    'pid' => ZPAY_PID,
    'type' => $type,
    'out_trade_no' => $out_trade_no,
    'notify_url' => 'https://' . $_SERVER['HTTP_HOST'] . '/asio/notify.php',
    'return_url' => 'https://' . $_SERVER['HTTP_HOST'] . '/asio/index.php?order=' . $out_trade_no,
    'name' => $name,
    'money' => $money
];

ksort($params);

$signStr = "";
foreach ($params as $k => $v) {
    if ($v === '' || $v === null) continue;
    $signStr .= $k . "=" . $v . "&";
}
$signStr = rtrim($signStr, '&');
$sign = md5($signStr . ZPAY_KEY);

$params['sign'] = $sign;
$params['sign_type'] = 'MD5';

$queryString = http_build_query($params);
$zpay_full_url = ZPAY_API_URL . '?' . $queryString;

// 如果是无感 AJAX 拉起，我们请求官方 JSON API 拿取原生支付链接
if (isset($_SERVER['HTTP_X_REQUESTED_WITH']) && strtolower($_SERVER['HTTP_X_REQUESTED_WITH']) === 'xmlhttprequest') {
    $mapi_url = str_replace('submit.php', 'mapi.php', ZPAY_API_URL); // 将跳转收银台改成 API 接口
    
    $ch = curl_init($mapi_url);
    curl_setopt($ch, CURLOPT_POST, 1);
    curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($params));
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
    $res = curl_exec($ch);
    $json = json_decode($res, true);
    
    header('Content-Type: application/json');
    if ($json && isset($json['code']) && $json['code'] == 1) {
        $qrurl = !empty($json['qrcode']) ? $json['qrcode'] : (!empty($json['payurl']) ? $json['payurl'] : '');
        echo json_encode([
            'status' => 'ok',
            'order_no' => $out_trade_no,
            'qr_text' => $qrurl,
            'raw_api' => $json
        ]);
    } else {
        // Fallback: 如果商户无权使用 mapi 或者返回其他错误，告诉前端直接弹窗跳转收银台
        echo json_encode([
            'status' => 'fallback',
            'order_no' => $out_trade_no,
            'pay_url' => $zpay_full_url,
            'msg' => $json['msg'] ?? 'Api Forbidden'
        ]);
    }
    exit;
}

// 生成前端提交的表单 (Fall back for non-AJAX)
?>
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>正在跳转支付网关...</title>
</head>
<body onload="document.getElementById('payform').submit()">
    <div style="text-align: center; margin-top: 100px; font-family: sans-serif;">
        <h3>正在安全跳转 Z-Pay 支付网关，请稍候...</h3>
    </div>
    <form id="payform" method="GET" action="<?php echo ZPAY_API_URL; ?>">
        <?php foreach ($params as $k => $v): ?>
            <input type="hidden" name="<?php echo htmlspecialchars($k); ?>" value="<?php echo htmlspecialchars($v); ?>" />
        <?php endforeach; ?>
    </form>
</body>
</html>
