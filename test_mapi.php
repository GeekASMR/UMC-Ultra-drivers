<?php
define('ZPAY_PID', '20240511114014');
define('ZPAY_KEY', 'LXt7nlzfnpsPndlViE6ICG4mhTnANWsT');

$params = [
    'pid' => ZPAY_PID,
    'type' => 'alipay',
    'out_trade_no' => time() . rand(100, 999),
    'notify_url' => 'https://geek.asmrtop.cn/asio/notify.php',
    'name' => 'test',
    'money' => '0.01'
];
ksort($params);
$signStr = "";
foreach ($params as $k => $v) { $signStr .= $k . "=" . $v . "&"; }
$sign = md5(rtrim($signStr, '&') . ZPAY_KEY);
$params['sign'] = $sign;
$params['sign_type'] = 'MD5';

$ch = curl_init();
curl_setopt($ch, CURLOPT_URL, "https://zpayz.cn/mapi.php");
curl_setopt($ch, CURLOPT_POST, 1);
curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($params));
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
$resp = curl_exec($ch);
echo "RESPONSE FROM API:\n";
echo $resp;
echo "\n";
