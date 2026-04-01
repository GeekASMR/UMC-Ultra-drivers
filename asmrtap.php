<?php
session_start();
require_once 'config.php';

// ---- ASIO Compatibility Test Data API (No Auth Required for GET/Public POST) ----
if (isset($_GET['api']) && $_GET['api'] === 'test_logs') {
    $pdo = getDb();
    try {
        $pdo->exec("CREATE TABLE IF NOT EXISTS asio_test_logs (id BIGINT PRIMARY KEY, card VARCHAR(100), host VARCHAR(50), wdm VARCHAR(50), buffer VARCHAR(50), algo VARCHAR(255), status VARCHAR(20), desc_text TEXT, feel TEXT, audio LONGTEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
    } catch(Exception $e) {}

    if ($_SERVER['REQUEST_METHOD'] === 'POST') {
        $data = json_decode(file_get_contents('php://input'), true);
        if (!$data) { http_response_code(400); exit; }
        
        if (isset($data['action']) && $data['action'] === 'delete') {
            if (empty($data['auth']) || $data['auth'] !== 'asmrtop') { http_response_code(403); exit; }
            
            // Delete physical file if exists
            $stmt = $pdo->prepare("SELECT audio FROM asio_test_logs WHERE id = ?");
            $stmt->execute([$data['id']]);
            $row = $stmt->fetch();
            if ($row && !empty($row['audio']) && strpos($row['audio'], '/asio/uploads/') === 0) {
                $filePath = __DIR__ . '/uploads/' . basename($row['audio']);
                if (file_exists($filePath)) @unlink($filePath);
            }
            
            $pdo->prepare("DELETE FROM asio_test_logs WHERE id = ?")->execute([$data['id']]);
            echo json_encode(['ok'=>1]);
            exit;
        }
        if (isset($data['action']) && $data['action'] === 'clear') {
            if (empty($data['auth']) || $data['auth'] !== 'asmrtop') { http_response_code(403); exit; }
            
            // Wipe all files in uploads dir
            $uploadDir = __DIR__ . '/uploads';
            if (is_dir($uploadDir)) {
                $files = glob($uploadDir . '/*');
                foreach($files as $file) { if(is_file($file)) @unlink($file); }
            }
            
            $pdo->exec("DELETE FROM asio_test_logs");
            echo json_encode(['ok'=>1]);
            exit;
        }
        
        $audioUrl = '';
        if (!empty($data['audio']) && strpos($data['audio'], 'data:audio/') === 0) {
            $base64 = $data['audio'];
            $ext = 'wav';
            if (strpos($base64, 'audio/mpeg') !== false || strpos($base64, 'audio/mp3') !== false) $ext = 'mp3';
            
            // 剥离 Base64 头
            $parts = explode(',', $base64);
            if (count($parts) === 2) {
                $decoded = base64_decode($parts[1]);
                $uploadDir = __DIR__ . '/uploads';
                if (!is_dir($uploadDir)) @mkdir($uploadDir, 0755, true);
                
                $filename = 'test_' . $data['id'] . '.' . $ext;
                file_put_contents($uploadDir . '/' . $filename, $decoded);
                // 存入相对于域名的绝对路径，防止 HTML 与 PHP 不同级引发 404
                $audioUrl = '/asio/uploads/' . $filename; 
            }
        } else if (!empty($data['audio'])) {
            $audioUrl = $data['audio']; // 已经是 URL 的情况
        }
        
        if (!empty($data['id'])) {
            $stmt = $pdo->prepare("INSERT INTO asio_test_logs (id, card, host, wdm, buffer, algo, status, desc_text, feel, audio) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE status=status");
            $stmt->execute([$data['id'], $data['card'], $data['host'], $data['wdm'], $data['buffer'], $data['algo'], $data['status'], $data['desc'], $data['feel'], $data['audio'] ?? '']);
            // update with actual file URL now
            $stmt2 = $pdo->prepare("UPDATE asio_test_logs SET audio = ? WHERE id = ?");
            $stmt2->execute([$audioUrl, $data['id']]);
            echo json_encode(['ok'=>1]);
        }
        exit;
    }
    
    header('Content-Type: application/json');
    $rows = $pdo->query("SELECT * FROM asio_test_logs ORDER BY id DESC")->fetchAll(PDO::FETCH_ASSOC);
    $out = [];
    foreach ($rows as $r) {
        $out[] = [
            'id' => (int)$r['id'],
            'card' => $r['card'],
            'host' => $r['host'],
            'wdm' => $r['wdm'],
            'buffer' => $r['buffer'],
            'algo' => $r['algo'],
            'status' => $r['status'],
            'desc' => $r['desc_text'],
            'feel' => $r['feel'],
            'audio' => $r['audio']
        ];
    }
    echo json_encode($out);
    exit;
}
// ---- END API ----

// ---- 管理员密码设置 ----
define('ADMIN_PASS', 'Asmrtop2025.'); // 修改为你想要的后台登录密码

// 登录验证
if (isset($_POST['login_user']) && isset($_POST['login_pass'])) {
    $pdo = getDb();
    try { $pdo->exec("ALTER TABLE licenses ADD COLUMN creator VARCHAR(50) DEFAULT 'admin'"); } catch(Exception $e){}
    try { $pdo->exec("CREATE TABLE IF NOT EXISTS sub_admins (id INT AUTO_INCREMENT PRIMARY KEY, username VARCHAR(50) UNIQUE, password VARCHAR(100), created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"); } catch(Exception $e){}

    $u = trim($_POST['login_user']);
    $p = trim($_POST['login_pass']);
    if ($u === 'admin' && $p === ADMIN_PASS) {
        $_SESSION['admin_logged_in'] = true;
        $_SESSION['role'] = 'admin';
        $_SESSION['admin_user'] = 'admin';
        header("Location: " . basename($_SERVER['PHP_SELF']));
        exit;
    } else {
        $stmt = $pdo->prepare("SELECT * FROM sub_admins WHERE username = ? AND password = ?");
        $stmt->execute([$u, $p]);
        $sub = $stmt->fetch();
        if ($sub) {
            $_SESSION['admin_logged_in'] = true;
            $_SESSION['role'] = 'sub';
            $_SESSION['admin_user'] = $sub['username'];
            header("Location: " . basename($_SERVER['PHP_SELF']));
            exit;
        } else {
            $error = "账号或密码错误！";
        }
    }
}

// 退出登录
if (isset($_GET['logout'])) {
    session_destroy();
    header("Location: " . basename($_SERVER['PHP_SELF']));
    exit;
}

// 检查是否登录
if (empty($_SESSION['admin_logged_in'])) {
?>
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>UMC Ultra 授权管理系统</title>
    <style>
        body { background: #0f172a; color: #fff; font-family: sans-serif; display: flex; height: 100vh; justify-content: center; align-items: center; margin: 0; }
        .login-box { background: #1e293b; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); text-align: center; }
        input { padding: 10px; border: 1px solid #334155; border-radius: 4px; background: #0f172a; color: #fff; margin-bottom: 20px; outline: none; }
        button { padding: 10px 20px; background: #3b82f6; border: none; color: #fff; border-radius: 4px; cursor: pointer; }
        button:hover { background: #2563eb; }
    </style>
</head>
<body>
    <div class="login-box">
        <h2>UMC Ultra 授权调度网关</h2>
        <?php if (!empty($error)) echo "<p style='color:#ef4444'>$error</p>"; ?>
        <form method="post">
            <input type="text" name="login_user" placeholder="管理员 / 代理子账号" required autofocus><br>
            <input type="password" name="login_pass" placeholder="访问安全口令" required><br>
            <button type="submit">接入系统</button>
        </form>
    </div>
</body>
</html>
<?php
    exit;
}

$pdo = getDb();
try { $pdo->exec("ALTER TABLE licenses ADD COLUMN creator VARCHAR(50) DEFAULT 'admin'"); } catch(Exception $e){}
try { $pdo->exec("CREATE TABLE IF NOT EXISTS sub_admins (id INT AUTO_INCREMENT PRIMARY KEY, username VARCHAR(50) UNIQUE, password VARCHAR(100), created_at DATETIME DEFAULT CURRENT_TIMESTAMP)"); } catch(Exception $e){}

$role = $_SESSION['role'] ?? 'admin';
$currentUser = $_SESSION['admin_user'] ?? 'admin';

// ---- 价格配置修改 ----
if (isset($_POST['action']) && $_POST['action'] === 'update_price' && $role === 'admin') {
    $newPrice = number_format((float)$_POST['new_price'], 2, '.', '');
    if ($newPrice > 0) {
        $upd = $pdo->prepare("INSERT INTO settings (setting_key, setting_value) VALUES ('price', ?) ON DUPLICATE KEY UPDATE setting_value = ?");
        $upd->execute([$newPrice, $newPrice]);
        header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode("售卖价格已成功更新为： ¥ $newPrice"));
        exit;
    }
}

if (!empty($_GET['msg'])) {
    $msg = $_GET['msg'];
}

// ---- 手工生成卡密 ----
if (isset($_POST['action']) && $_POST['action'] === 'generate_key') {
    $licenseKey = 'UMCA-' 
        . strtoupper(bin2hex(random_bytes(2))) . '-'
        . strtoupper(bin2hex(random_bytes(2))) . '-'
        . strtoupper(bin2hex(random_bytes(2)));
    
    $ins = $pdo->prepare("INSERT INTO licenses (license_key, max_machines, machines_bound, order_no, creator) VALUES (?, ?, '[]', 'Manual-Gen', ?)");
    $ins->execute([$licenseKey, LICENSE_MAX_MACHINES, $currentUser]);
    $msg = "成功生成卡密： $licenseKey";
    header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode($msg) . "#tab-licenses");
    exit;
}

// ---- 清空重置某个卡密的绑机状态 ----
if (isset($_GET['reset_id'])) {
    $upd = $pdo->prepare("UPDATE licenses SET machines_bound = '[]' WHERE id = ?");
    $upd->execute([(int)$_GET['reset_id']]);
    $script_name = basename($_SERVER['PHP_SELF']);
    header("Location: $script_name#tab-licenses");
    exit;
}

// ---- 删除卡密 ----
if (isset($_GET['del_license_id'])) {
    $del = $pdo->prepare("DELETE FROM licenses WHERE id = ?");
    $del->execute([(int)$_GET['del_license_id']]);
    $script_name = basename($_SERVER['PHP_SELF']);
    header("Location: $script_name?msg=" . urlencode("卡密已成功彻底删除！") . "#tab-licenses");
    exit;
}

// ---- 更新卡密备注 ----
if (isset($_POST['action']) && $_POST['action'] === 'update_remark') {
    $upd = $pdo->prepare("UPDATE licenses SET remark = ? WHERE id = ?");
    $upd->execute([$_POST['remark'], (int)$_POST['license_id']]);
    header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode("卡密备注已更新！") . "#tab-licenses");
    exit;
}

// ---- 子后台管理 (仅admin) ----
if (isset($_POST['action']) && $_POST['action'] === 'add_subadmin' && $role === 'admin') {
    $su = trim($_POST['sub_user']);
    $sp = trim($_POST['sub_pass']);
    if ($su && $sp) {
        $pdo->prepare("INSERT IGNORE INTO sub_admins (username, password) VALUES (?, ?)")->execute([$su, $sp]);
        header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode("新增代理账号成功： $su") . "#tab-subs");
        exit;
    }
}
if (isset($_GET['del_subadmin']) && $role === 'admin') {
    $pdo->prepare("DELETE FROM sub_admins WHERE id = ?")->execute([(int)$_GET['del_subadmin']]);
    header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode("代理账号已删除重置！") . "#tab-subs");
    exit;
}

// ---- 清空未付款订单 ----
if (isset($_GET['del_unpaid']) && $role === 'admin') {
    $del = $pdo->prepare("DELETE FROM orders WHERE status != 'paid'");
    $del->execute();
    header("Location: " . basename($_SERVER['PHP_SELF']) . "?msg=" . urlencode("未支付订单已经全部一键清空！") . "#tab-orders");
    exit;
}

// ---- 下载用户试用时间重置工具 ----
if (isset($_GET['action']) && $_GET['action'] === 'download_reset') {
    header("Content-Type: application/octet-stream");
    header("Content-Disposition: attachment; filename=Reset_UMC_Trial.bat");
    echo "@echo off\r\n";
    echo "echo [UMC Ultra Professional] Resetting local trial time...\r\n";
    echo "reg delete \"HKCU\\Software\\ASMRTOP\\UMCUltra\" /v InstallTime /f >nul 2>&1\r\n";
    echo "echo Trial time has been successfully reset! You have another 60 minutes.\r\n";
    echo "pause\r\n";
    exit;
}

if (isset($_GET['reset_machine'])) {
    $mId = $_GET['reset_machine'];
    $upd = $pdo->prepare("UPDATE machines SET reset_flag = 1 WHERE machine_id = ?");
    $upd->execute([$mId]);
    $script_name = basename($_SERVER['PHP_SELF']);
    header("Location: $script_name?msg=" . urlencode("重置试用时长指令已下发！在线终端将自动恢复为 60 分钟试用。") . "#tab-users");
    exit;
}

// ---- 删除日志 ----
if (isset($_GET['del_log'])) {
    $rel = str_replace("\0", '', $_GET['del_log']);
    if (strpos($rel, '..') === false) {
        $logPath = realpath(__DIR__ . '/logs/' . $rel);
        if ($logPath && strpos($logPath, realpath(__DIR__ . '/logs/')) === 0) {
            unlink($logPath);
            $script_name = basename($_SERVER['PHP_SELF']);
            header("Location: $script_name?msg=" . urlencode("日志文件已成功删除！") . "#tab-logs");
            exit;
        }
    }
}

// ---- 查看日志内容 ----
if (isset($_GET['view_log'])) {
    $rel = str_replace("\0", '', $_GET['view_log']);
    if (strpos($rel, '..') === false) {
        $logPath = realpath(__DIR__ . '/logs/' . $rel);
        if ($logPath && strpos($logPath, realpath(__DIR__ . '/logs/')) === 0) {
            header("Content-Type: text/plain; charset=utf-8");
            readfile($logPath);
            exit;
        }
    }
    die("❌ 找不到指定的日志文件！");
}

// 查询数据
$totalOrders = $pdo->query("SELECT COUNT(*) FROM orders WHERE status='paid'")->fetchColumn();
$totalRev = $pdo->query("SELECT SUM(money) FROM orders WHERE status='paid'")->fetchColumn() ?: '0.00';
$totalLicenses = $pdo->query("SELECT COUNT(*) FROM licenses")->fetchColumn();

// --- 搜索功能 ---
$searchQ = $_GET['q'] ?? '';
$whereLic = "";
$whereOrd = "";
$paramsLic = [];
$paramsOrd = [];

// --- 角色权限过滤 ---
if ($role === 'sub') {
    if ($whereLic) {
        $whereLic .= " AND creator = ?";
    } else {
        $whereLic = "WHERE creator = ?";
    }
    $paramsLic[] = $currentUser;
}

$stmtL = $pdo->prepare("SELECT * FROM licenses $whereLic ORDER BY id DESC LIMIT 500");
$stmtL->execute($paramsLic);
$licenses = $stmtL->fetchAll();

$stmtO = $pdo->prepare("SELECT * FROM orders $whereOrd ORDER BY id DESC LIMIT 500");
$stmtO->execute($paramsOrd);
$orders = $stmtO->fetchAll();

// Get the user's generated license keys array to filter machines for sub admin
$myKeysArr = array_column($licenses, 'license_key');

try {
    $all_machines = $pdo->query("SELECT * FROM machines ORDER BY last_seen DESC LIMIT 500")->fetchAll();
} catch (Exception $e) {
    $all_machines = [];
}

// Build machine_id -> license lookup for user dashboard
$machineLicenseMap = [];
$machines = []; // Filtered machines if sub
foreach ($licenses as $lic) {
    $boundMachines = json_decode($lic['machines_bound'], true) ?? [];
    foreach ($boundMachines as $mid) {
        $machineLicenseMap[$mid] = [
            'key' => $lic['license_key'],
            'remark' => $lic['remark'] ?? '',
            'creator' => $lic['creator'] ?? 'admin'
        ];
    }
}

foreach ($all_machines as $m) {
    $mid = $m['machine_id'];
    $m_creator = isset($machineLicenseMap[$mid]) ? $machineLicenseMap[$mid]['creator'] : null;
    if ($role === 'admin' || $m_creator === $currentUser) {
        // If admin, show all. If sub, only show if this machine is bound to a key they created.
        $machines[] = $m;
    }
}

// 查询日志文件 (支持用户子目录)
$logFiles = array_merge(
    (array)glob(__DIR__ . '/logs/*.log'),
    (array)glob(__DIR__ . '/logs/*/*.log')
);
if (!empty($logFiles)) {
    usort($logFiles, function($a, $b) {
        return filemtime($b) - filemtime($a);
    });
} else {
    $logFiles = [];
}

$subAdmins = [];
if ($role === 'admin') {
    $subAdmins = $pdo->query("SELECT * FROM sub_admins ORDER BY id DESC")->fetchAll();
}
?>
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>UMC Ultra 管理后台</title>
    <style>
        body { background: #111827; color: #f3f4f6; font-family: 'Segoe UI', sans-serif; margin: 0; padding: 20px; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #374151; padding-bottom: 20px; margin-bottom: 20px; }
        .stats { display: flex; gap: 20px; margin-bottom: 30px; }
        .stat-card { background: #1f2937; padding: 20px; border-radius: 8px; flex: 1; border: 1px solid #374151; text-align: center;}
        .stat-card h3 { margin: 0 0 10px 0; color: #9ca3af; font-size: 14px; text-transform: uppercase; }
        .stat-card p { margin: 0; font-size: 28px; font-weight: bold; color: #10b981; }
        table { width: 100%; border-collapse: collapse; margin-bottom: 40px; background: #1f2937; border-radius: 8px; overflow: hidden;}
        th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid #374151; }
        th { background: #374151; color: #d1d5db; }
        .success { color: #10b981; } .pending { color: #fbbf24; }
        .btn { padding: 8px 16px; background: #3b82f6; color: #fff; border: none; border-radius: 4px; cursor: pointer; text-decoration: none; font-size: 14px;}
        .btn-danger { background: #ef4444; }
        .msg { background: #064e3b; color: #34d399; padding: 15px; border-radius: 4px; border: 1px solid #10b981; margin-bottom: 20px; font-weight: bold;}
    </style>
</head>
<body>

<div class="header">
    <div style="display:flex;align-items:center;gap:15px;">
        <h2>UMC Ultra 授权大盘调度引擎</h2>
        <span class="btn" style="background:<?php echo $role==='admin'?'#ef4444':'#3b82f6'; ?>; font-size:12px; pointer-events:none;">身份状态: [<?php echo strtoupper($currentUser); ?>]</span>
    </div>
    <a href="?logout=1" class="btn btn-danger">断开安全链路 (退出)</a>
</div>

<?php if (!empty($msg)) echo "<div class='msg'>" . htmlspecialchars($msg) . "</div>"; ?>

<?php if ($role === 'admin'): ?>
<div class="stats">
    <div class="stat-card"><h3>成功订单数</h3><p><?php echo $totalOrders; ?></p></div>
    <div class="stat-card"><h3>总收入 (元)</h3><p>¥ <?php echo $totalRev; ?></p></div>
    <div class="stat-card"><h3>发卡总池统计</h3><p><?php echo $totalLicenses; ?></p></div>
</div>
<?php endif; ?>

<div class="header" style="border:none; padding:0;">
    <h3>日常管理工具</h3>
</div>

<div style="background: #1f2937; padding: 20px; border-radius: 8px; margin-bottom: 30px; border: 1px solid #374151; display: flex; gap: 30px; align-items: flex-end; flex-wrap: wrap;">
    <?php if ($role === 'admin'): ?>
    <form method="post" style="margin:0; flex:1; min-width:250px;">
        <label style="display:block; margin-bottom: 8px; color: #9ca3af; font-size:14px;">动态修改首页售价 (当前: ¥ <?php echo LICENSE_PRICE; ?>)</label>
        <div style="display:flex; gap:10px;">
            <input type="hidden" name="action" value="update_price">
            <input type="number" step="0.01" name="new_price" value="<?php echo LICENSE_PRICE; ?>" style="margin:0; flex:1; padding:10px; border:1px solid #475569; background:#0f172a; color:white; border-radius:4px;" required>
            <button type="submit" class="btn" style="background: #10b981;">确认同步</button>
        </div>
    </form>
    <?php endif; ?>
    
    <form method="GET" style="margin:0; flex:2; min-width:350px;">
        <label style="display:block; margin-bottom: 8px; color: #9ca3af; font-size:14px;">全局搜索 (支持卡密/本站订单号/支付方交易号)</label>
        <div style="display:flex; gap:10px;">
            <input type="text" name="q" placeholder="输入搜索关键词..." value="<?php echo htmlspecialchars($searchQ); ?>" style="margin:0; flex:1; padding:10px; border:1px solid #475569; background:#0f172a; color:white; border-radius:4px;">
            <button type="submit" class="btn" style="background: #eab308; color: #000; font-weight: bold;">查询</button>
            <?php if($searchQ): ?><a href="<?php echo basename($_SERVER['PHP_SELF']); ?>" class="btn" style="background: #4b5563;">清除</a><?php endif; ?>
        </div>
    </form>

    <form method="post" style="margin:0; display:flex; align-items:flex-end;">
        <input type="hidden" name="action" value="generate_key">
        <button type="submit" class="btn" style="padding: 11px 20px; background: #8b5cf6;">➕ 手工发卡</button>
    </form>
    
    <div style="margin:0; display:flex; align-items:flex-end;">
        <a href="asmrtap.php?action=download_reset" class="btn" style="padding: 11px 20px; background: #ea580c; text-decoration:none;">📥 下载重置工具</a>
    </div>
</div>

<div class="tabs" style="margin-bottom: 20px; border-bottom: 1px solid #374151;">
    <button class="tab-btn active" onclick="switchTab('tab-users')" style="padding: 10px 20px; background: transparent; color: white; border: none; border-bottom: 2px solid #38bdf8; cursor: pointer; font-size: 16px;">💻 用户大盘 (<?php echo count($machines); ?>)</button>
    <button class="tab-btn" onclick="switchTab('tab-licenses')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">🔑 卡密管理系统</button>
    <?php if ($role === 'admin'): ?>
    <button class="tab-btn" onclick="switchTab('tab-subs')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">👥 代理子号管理</button>
    <button class="tab-btn" onclick="switchTab('tab-orders')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">💰 聚合订单流水</button>
    <button class="tab-btn" onclick="switchTab('tab-logs')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">📜 终端重载诊断</button>
    <?php endif; ?>
</div>

<div id="tab-licenses" class="tab-content" style="display: none;">
<h3>许可密钥管理 (Licenses) <?php if($searchQ) echo "<span style='color:#38bdf8;font-size:16px;'>- 搜索结果</span>"; ?></h3>
<table>
    <tr>
        <th>ID</th>
        <?php if($role==='admin') echo "<th>开卡归属代理</th>"; ?>
        <th>激活码 (License Key)</th>
        <th>机器授权数</th>
        <th>状态</th>
        <th>关联订单备注</th>
        <th>自定义备注</th>
        <th>最近激活时间</th>
        <th>操作指引</th>
    </tr>
    <?php foreach ($licenses as $l): 
        $bound_machines = json_decode($l['machines_bound'], true) ?? [];
        $mc = count($bound_machines);
    ?>
    <tr>
        <td><?php echo $l['id']; ?></td>
        <?php if($role==='admin') echo "<td style='font-weight:bold;color:#fcd34d'>" . htmlspecialchars($l['creator'] ?? 'admin') . "</td>"; ?>
        <td style="font-family: monospace; color:#fbbf24;"><?php echo $l['license_key']; ?></td>
        <td><?php echo $mc . ' / ' . $l['max_machines']; ?></td>
        <td><?php echo $l['status'] == 'active' ? '<span class="success">正常</span>' : '<span class="btn-danger">封禁</span>'; ?></td>
        <td><?php echo $l['order_no']; ?></td>
        <td>
            <form method="post" style="display:flex; gap:5px; margin:0; line-height:1;">
                <input type="hidden" name="action" value="update_remark">
                <input type="hidden" name="license_id" value="<?php echo $l['id']; ?>">
                <input type="text" name="remark" value="<?php echo htmlspecialchars($l['remark'] ?? ''); ?>" placeholder="点击添加..." style="width:120px; padding:4px 8px; font-size:12px; margin:0;" onblur="this.form.submit()">
                <button type="submit" style="display:none;">保存</button>
            </form>
        </td>
        <td><?php echo $l['last_activated'] ?: '从未激活'; ?></td>
        <td>
            <a href="?reset_id=<?php echo $l['id']; ?>" class="btn btn-warning" style="background:#f59e0b;color:white;text-decoration:none;padding:4px 8px;font-size:12px;" onclick="return confirm('确定解绑此卡密的所有机器吗？')">清空重置设备</a>
            <a href="?del_license_id=<?php echo $l['id']; ?>" class="btn btn-danger" style="text-decoration:none; margin-left:4px;padding:4px 8px;font-size:12px;" onclick="return confirm('确定要彻底永久删除此卡密吗？本操作不可逆复原！\n如果此卡密已被客户激活，客户将会在下次强制断网核对时被封禁！')">彻底删除</a>
        </td>
    </tr>
    <?php endforeach; ?>
</table>
</div>

<div id="tab-orders" class="tab-content" style="display: none;">
<h3 style="display:flex; justify-content:space-between; align-items:center;">
    <span>最近订单 (Orders)</span>
    <a href="?del_unpaid=1" class="btn btn-warning" style="background:#ea580c;color:white;text-decoration:none;font-size:14px;padding:6px 12px;border-radius:4px;" onclick="return confirm('确定要一键清空所有尚未完成支付的冗余订单吗？')">🗑️ 一键清空未付款订单</a>
</h3>
<table>
    <tr>
        <th>ID</th>
        <th>本站订单号</th>
        <th>系统交易号</th>
        <th>金额</th>
        <th>方式</th>
        <th>状态</th>
        <th>发放卡密</th>
        <th>时间</th>
    </tr>
    <?php foreach ($orders as $o): ?>
    <tr>
        <td><?php echo $o['id']; ?></td>
        <td><?php echo $o['out_trade_no']; ?></td>
        <td><?php echo $o['trade_no'] ?: '-'; ?></td>
        <td class="success">¥ <?php echo $o['money']; ?></td>
        <td><?php echo $o['pay_type']; ?></td>
        <td class="<?php echo $o['status']=='paid'?'success':'pending'; ?>"><?php echo $o['status']=='paid' ? '已支付' : '未付'; ?></td>
        <td style="font-family: monospace; color:#fbbf24;"><?php echo $o['license_key'] ?: '-'; ?></td>
        <td><?php echo $o['created_at']; ?></td>
    </tr>
    <?php endforeach; ?>
</table>
</div>

<?php if ($role === 'admin'): ?>
<div id="tab-subs" class="tab-content" style="display: none;">
<h3>代理分发矩阵配置</h3>
<div style="background:#1f2937; padding:20px; border:1px solid #374151; border-radius:8px; margin-bottom:20px;">
    <form method="post" style="display:flex; gap:10px; align-items:flex-end; margin:0;">
        <input type="hidden" name="action" value="add_subadmin">
        <div>
            <label style="display:block; color:#9ca3af; font-size:13px; margin-bottom:4px;">代理登录帐号</label>
            <input type="text" name="sub_user" required style="padding:10px; border-radius:4px; border:1px solid #475569; background:#0f172a; color:#fff;" placeholder="输入新的用户名">
        </div>
        <div>
            <label style="display:block; color:#9ca3af; font-size:13px; margin-bottom:4px;">赋予安全密码</label>
            <input type="password" name="sub_pass" required style="padding:10px; border-radius:4px; border:1px solid #475569; background:#0f172a; color:#fff;" placeholder="给下级设定的密码">
        </div>
        <button type="submit" class="btn" style="background:#10b981; margin-bottom:2px;">➕ 分配加入代理矩阵</button>
    </form>
</div>
<table>
    <tr><th>ID</th><th>创建时间</th><th>代理名称 (账户)</th><th>核编收回操作</th></tr>
    <?php foreach ($subAdmins as $sa): ?>
    <tr>
        <td><?php echo $sa['id']; ?></td>
        <td><?php echo $sa['created_at']; ?></td>
        <td style="font-weight:bold;color:#60a5fa"><?php echo htmlspecialchars($sa['username']); ?></td>
        <td><a href="?del_subadmin=<?php echo $sa['id']; ?>" class="btn btn-danger" onclick="return confirm('确认切断该代理的一切管理权限？')">彻底抹除账号</a></td>
    </tr>
    <?php endforeach; ?>
</table>
</div>
<?php endif; ?>

<div id="tab-users" class="tab-content">
<h3 style="color:#38bdf8;">用户终端大盘 (<?php echo $role==='admin'?'全网汇聚':'本代理所属'; ?> 设备心跳数据)</h3>
<table>
    <tr>
        <th>硬件指纹识别 ID (Machine)</th>
        <?php if($role==='admin') echo "<th>供卡代理者</th>"; ?>
        <th>设备注册层级 (Status)</th>
        <th>绑定流转卡密 (License)</th>
        <th>自定义客户备注</th>
        <th>试用剩余时长 (Trial Limit)</th>
        <th>最后心跳时间 (Last Seen)</th>
        <th>远程操作</th>
    </tr>
    <?php foreach ($machines as $m): 
        $mid = $m['machine_id'];
        $boundLic = $machineLicenseMap[$mid] ?? null;
    ?>
    <tr>
        <td style="font-family: monospace; color:#a7f3d0;"><?php echo htmlspecialchars($mid); ?></td>
        <?php if($role==='admin') echo "<td style='font-weight:bold;color:#fcd34d'>" . ($boundLic ? htmlspecialchars($boundLic['creator']) : '-') . "</td>"; ?>
        <td style="font-weight:bold; color:<?php echo $m['status'] == 'active' ? '#10b981' : '#f59e0b'; ?>"><?php echo $m['status'] == 'active' ? '授衔绑定激活' : '白名单试用期'; ?></td>
        <td style="font-family:monospace; color:#fbbf24; font-size:12px;"><?php echo $boundLic ? htmlspecialchars($boundLic['key']) : '<span style="color:#64748b;">本地独立试用计算中</span>'; ?></td>
        <td style="color:#94a3b8; font-size:13px;"><?php echo $boundLic && $boundLic['remark'] ? htmlspecialchars($boundLic['remark']) : '<span style="color:#475569;">-</span>'; ?></td>
        <td>
            <?php 
                if ($m['status'] == 'active') echo '<span style="color:#64748b;">无限制</span>';
                else if ((int)$m['rem_minutes'] <= 0) echo '<span style="color:#ef4444;">已过期 (0分钟)</span>';
                else echo '<span style="color:#fcd34d;">剩余 ' . $m['rem_minutes'] . ' 分钟</span>';
                if ($m['reset_flag']) echo ' <span style="font-size:12px;color:#38bdf8;">(重置下发中...)</span>';
            ?>
        </td>
        <td><?php echo $m['last_seen']; ?></td>
        <td>
            <?php if ($m['status'] !== 'active'): ?>
            <a href="?reset_machine=<?php echo urlencode($m['machine_id']); ?>" class="btn btn-warning" style="background:#ea580c;color:white;text-decoration:none;border-radius:4px;padding:4px 8px;font-size:13px;" onclick="return confirm('确定重置该终端的试用时长吗？客户下次载入时会自动收到重置信号。')">重置试用时长</a>
            <?php else: ?>
            <span style="color: #64748b; font-size:13px;">无须重置</span>
            <?php endif; ?>
        </td>
    </tr>
    <?php endforeach; ?>
</table>
</div>
<?php if ($role === 'admin'): ?>
<div id="tab-logs" class="tab-content" style="display: none;">
<h3 style="color:#38bdf8;">用户终端诊断日志 (Diagnostic Logs)</h3>
<table>
    <tr>
        <th>日志文件名 (Log File)</th>
        <th>大小 (Size)</th>
        <th>上传时间 (Upload Time)</th>
        <th>操作 (Actions)</th>
    </tr>
    <?php foreach ($logFiles as $lf): 
        $relPath = str_replace(__DIR__ . DIRECTORY_SEPARATOR . 'logs' . DIRECTORY_SEPARATOR, '', $lf);
        $relPath = str_replace('\\', '/', $relPath); // 统一为正斜杠以便 URL 传输
    ?>
    <tr>
        <td style="font-family: monospace; color:#fbbf24;"><?php echo htmlspecialchars($relPath); ?></td>
        <td><?php echo round(filesize($lf)/1024, 1); ?> KB</td>
        <td><?php echo date("Y-m-d H:i:s", filemtime($lf)); ?></td>
        <td>
            <a href="?view_log=<?php echo urlencode($relPath); ?>" target="_blank" class="btn" style="background:#10b981; text-decoration:none; padding:4px 8px; font-size:13px;">👁️ 查看 (View)</a>
            <a href="?del_log=<?php echo urlencode($relPath); ?>" class="btn btn-danger" style="text-decoration:none; padding:4px 8px; font-size:13px;" onclick="return confirm('确定永久删除该诊断日志吗？')">🗑️ 删除</a>
        </td>
    </tr>
    <?php endforeach; ?>
</table>
</div>
<?php endif; ?>

<script>
function switchTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(el => el.style.display = 'none');
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.style.borderBottom = 'none';
        btn.style.color = '#9ca3af';
    });
    
    document.getElementById(tabId).style.display = 'block';
    const activeBtn = Array.from(document.querySelectorAll('.tab-btn')).find(b => b.getAttribute('onclick').includes(tabId));
    if (activeBtn) {
        activeBtn.style.borderBottom = '2px solid #38bdf8';
        activeBtn.style.color = 'white';
    }
}

// Check for hash on load to switch to specific tab
document.addEventListener("DOMContentLoaded", function() {
    let hash = window.location.hash;
    if(hash && document.getElementById(hash.substring(1))) {
        switchTab(hash.substring(1));
    }
});
</script>

<div style="height:100px;"></div>

</body>
</html>
