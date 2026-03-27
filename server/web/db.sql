CREATE TABLE IF NOT EXISTS `licenses` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `license_key` VARCHAR(64) UNIQUE NOT NULL,
    `max_machines` INT DEFAULT 2,
    `machines_bound` JSON NOT NULL COMMENT '已绑定的机器ID数组',
    `status` VARCHAR(20) DEFAULT 'active' COMMENT 'active 或 revoked',
    `order_no` VARCHAR(64) DEFAULT NULL COMMENT '关联的支付订单号',
    `expiry_date` DATE DEFAULT NULL COMMENT '激活后的过期时间',
    `last_activated` TIMESTAMP NULL COMMENT '最近一次激活时间',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `orders` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `out_trade_no` VARCHAR(64) UNIQUE NOT NULL COMMENT '本站生成的订单号',
    `trade_no` VARCHAR(64) DEFAULT NULL COMMENT '支付平台的交易号',
    `money` DECIMAL(10,2) NOT NULL COMMENT '订单金额',
    `pay_type` VARCHAR(20) DEFAULT 'alipay' COMMENT '支付方式',
    `status` VARCHAR(20) DEFAULT 'pending' COMMENT 'pending / paid',
    `license_key` VARCHAR(64) DEFAULT NULL COMMENT '支付成功后分配的激活码',
    `paid_at` TIMESTAMP NULL COMMENT '支付时间',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
