<?php
/**
 * UMC Ultra 许可密钥批量生成工具
 * 
 * 用法: php generate_keys.php [数量]
 * 默认生成 10 个密钥
 */

$count = (int)($argv[1] ?? 10);
$keysFile = __DIR__ . '/keys.json';

// 加载已有密钥
$existing = [];
if (file_exists($keysFile)) {
    $existing = json_decode(file_get_contents($keysFile), true) ?? [];
}

$newKeys = [];
for ($i = 0; $i < $count; $i++) {
    $key = 'UMCA-' 
        . strtoupper(bin2hex(random_bytes(2))) . '-'
        . strtoupper(bin2hex(random_bytes(2))) . '-'
        . strtoupper(bin2hex(random_bytes(2)));
    
    $existing[$key] = [
        'machines' => [],
        'last_activated' => null,
        'expiry' => null,
        'created' => date('Y-m-d H:i:s'),
    ];
    $newKeys[] = $key;
}

file_put_contents($keysFile, json_encode($existing, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));

echo "已生成 $count 个新密钥:\n\n";
foreach ($newKeys as $k) {
    echo "  $k\n";
}
echo "\n总计 " . count($existing) . " 个密钥, 已写入 $keysFile\n";
