<?php
/**
 * UMC Ultra 服务端配置文件
 * 包含数据库和支付配置
 */

// 数据库配置
define('DB_HOST', '127.0.0.1');
define('DB_USER', 'geekasmr2026');
define('DB_PASS', 'kPDhX2MSxtpzCcfb');
define('DB_NAME', 'geekasmr2026');
define('DB_PORT', 3306);

// Z-Pay 支付系统配置
define('ZPAY_API_URL', 'https://zpayz.cn/submit.php');  // 收银台跳转网关
define('ZPAY_PID', '20240511114014');
define('ZPAY_KEY', 'LXt7nlzfnpsPndlViE6ICG4mhTnANWsT');

// 动态价格引擎 (规避宝塔文件组权限问题，直接存库)
if (!function_exists('getLicensePrice')) {
    function getLicensePrice() {
        $pdo = getDb();
        try {
            $stmt = $pdo->query("SELECT setting_value FROM settings WHERE setting_key = 'price'");
            $val = $stmt->fetchColumn();
            if ($val !== false) return $val;
        } catch (Exception $e) {
            // 表不存在时自动建表并塞入默认值
            try {
                $pdo->exec("CREATE TABLE IF NOT EXISTS settings (
                    setting_key VARCHAR(50) PRIMARY KEY,
                    setting_value VARCHAR(255)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
                $pdo->exec("INSERT IGNORE INTO settings (setting_key, setting_value) VALUES ('price', '29.90')");
            } catch (Exception $e2) { }
        }
        return '29.90';
    }
}
define('LICENSE_PRICE', getLicensePrice());

define('LICENSE_MAX_MACHINES', 2); // 每个激活码支持的最大电脑数量

// ASIO客户端激活混淆密钥 (必需和 C++ 代码 LIC_VERIFY_SALT 完全一致)
define('VERIFY_SALT', 'UMC_ULTRA_2026_ASMRTOP_SEC');

// PDO 错误处理全局配置
date_default_timezone_set('Asia/Shanghai');

function getDb() {
    static $pdo = null;
    if ($pdo === null) {
        $dsn = 'mysql:host=' . DB_HOST . ';dbname=' . DB_NAME . ';port=' . DB_PORT . ';charset=utf8mb4';
        try {
            $pdo = new PDO($dsn, DB_USER, DB_PASS, [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            ]);
            $pdo->exec("SET time_zone = '+08:00'");
        } catch (PDOException $e) {
            die("数据库连接失败: " . $e->getMessage());
        }
    }
    return $pdo;
}
