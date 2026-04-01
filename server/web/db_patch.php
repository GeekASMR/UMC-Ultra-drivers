<?php
require_once 'config.php';
$pdo = getDb();
try {
    $pdo->exec("ALTER TABLE licenses ADD COLUMN remark VARCHAR(255) DEFAULT NULL COMMENT '管理员对该卡密的备注'");
    echo "SUCCESS_REMARK_ADDED";
} catch (Exception $e) {
    echo "ERROR: " . $e->getMessage();
}
?>
