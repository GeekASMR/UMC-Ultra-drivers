<?php
session_start();
require_once 'config.php';

// ---- 管理员密码设置 ----
define('ADMIN_PASS', 'Asmrtop2025.'); // 修改为你想要的后台登录密码

// 登录验证
if (isset($_POST['login_pass'])) {
    if ($_POST['login_pass'] === ADMIN_PASS) {
        $_SESSION['admin_logged_in'] = true;
        header("Location: asmrtap.php");
        exit;
    } else {
        $error = "密码错误！";
    }
}

// 退出登录
if (isset($_GET['logout'])) {
    session_destroy();
    header("Location: asmrtap.php");
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
        <h2>UMC Ultra 后台登录</h2>
        <?php if (!empty($error)) echo "<p style='color:#ef4444'>$error</p>"; ?>
        <form method="post">
            <input type="password" name="login_pass" placeholder="请输入管理员密码" required autofocus><br>
            <button type="submit">登录</button>
        </form>
    </div>
</body>
</html>
<?php
    exit;
}

$pdo = getDb();

// ---- 价格配置修改 ----
if (isset($_POST['action']) && $_POST['action'] === 'update_price') {
    $newPrice = number_format((float)$_POST['new_price'], 2, '.', '');
    if ($newPrice > 0) {
        $upd = $pdo->prepare("INSERT INTO settings (setting_key, setting_value) VALUES ('price', ?) ON DUPLICATE KEY UPDATE setting_value = ?");
        $upd->execute([$newPrice, $newPrice]);
        header("Location: asmrtap.php?msg=" . urlencode("售卖价格已成功更新为： ¥ $newPrice"));
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
    
    $ins = $pdo->prepare("INSERT INTO licenses (license_key, max_machines, machines_bound, order_no) VALUES (?, ?, '[]', 'Manual-Gen')");
    $ins->execute([$licenseKey, LICENSE_MAX_MACHINES]);
    $msg = "成功生成卡密： $licenseKey";
    header("Location: asmrtap.php?msg=" . urlencode($msg));
    exit;
}

// ---- 清空重置某个卡密的绑机状态 ----
if (isset($_GET['reset_id'])) {
    $upd = $pdo->prepare("UPDATE licenses SET machines_bound = '[]' WHERE id = ?");
    $upd->execute([(int)$_GET['reset_id']]);
    header("Location: asmrtap.php");
    exit;
}

// ---- 删除卡密 ----
if (isset($_GET['del_license_id'])) {
    $del = $pdo->prepare("DELETE FROM licenses WHERE id = ?");
    $del->execute([(int)$_GET['del_license_id']]);
    header("Location: asmrtap.php?msg=" . urlencode("卡密已成功彻底删除！"));
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
    header("Location: asmrtap.php?msg=" . urlencode("重置试用时长指令已下发！在线终端将自动恢复为 60 分钟试用。"));
    exit;
}

// ---- 删除日志 ----
if (isset($_GET['del_log'])) {
    $rel = str_replace("\0", '', $_GET['del_log']);
    if (strpos($rel, '..') === false) {
        $logPath = realpath(__DIR__ . '/logs/' . $rel);
        if ($logPath && strpos($logPath, realpath(__DIR__ . '/logs/')) === 0) {
            unlink($logPath);
            header("Location: asmrtap.php?msg=" . urlencode("日志文件已成功删除！"));
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

if ($searchQ) {
    $whereLic = "WHERE license_key LIKE ? OR order_no LIKE ?";
    $paramsLic = ["%$searchQ%", "%$searchQ%"];
    $whereOrd = "WHERE out_trade_no LIKE ? OR trade_no LIKE ? OR license_key LIKE ?";
    $paramsOrd = ["%$searchQ%", "%$searchQ%", "%$searchQ%"];
}

$stmtL = $pdo->prepare("SELECT * FROM licenses $whereLic ORDER BY id DESC LIMIT 100");
$stmtL->execute($paramsLic);
$licenses = $stmtL->fetchAll();

$stmtO = $pdo->prepare("SELECT * FROM orders $whereOrd ORDER BY id DESC LIMIT 100");
$stmtO->execute($paramsOrd);
$orders = $stmtO->fetchAll();

try {
    $machines = $pdo->query("SELECT * FROM machines ORDER BY last_seen DESC LIMIT 100")->fetchAll();
} catch (Exception $e) {
    $machines = [];
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
    <h2>UMC Ultra 控制面板</h2>
    <a href="?logout=1" class="btn btn-danger">退出登录</a>
</div>

<?php if (!empty($msg)) echo "<div class='msg'>" . htmlspecialchars($msg) . "</div>"; ?>

<div class="stats">
    <div class="stat-card"><h3>成功订单数</h3><p><?php echo $totalOrders; ?></p></div>
    <div class="stat-card"><h3>总收入 (元)</h3><p>¥ <?php echo $totalRev; ?></p></div>
    <div class="stat-card"><h3>发卡总数</h3><p><?php echo $totalLicenses; ?></p></div>
</div>

<div class="header" style="border:none; padding:0;">
    <h3>系统管理工具</h3>
</div>

<div style="background: #1f2937; padding: 20px; border-radius: 8px; margin-bottom: 30px; border: 1px solid #374151; display: flex; gap: 30px; align-items: flex-end; flex-wrap: wrap;">
    <form method="post" style="margin:0; flex:1; min-width:250px;">
        <label style="display:block; margin-bottom: 8px; color: #9ca3af; font-size:14px;">动态修改首页售价 (当前: ¥ <?php echo LICENSE_PRICE; ?>)</label>
        <div style="display:flex; gap:10px;">
            <input type="hidden" name="action" value="update_price">
            <input type="number" step="0.01" name="new_price" value="<?php echo LICENSE_PRICE; ?>" style="margin:0; flex:1; padding:10px; border:1px solid #475569; background:#0f172a; color:white; border-radius:4px;" required>
            <button type="submit" class="btn" style="background: #10b981;">确认同步</button>
        </div>
    </form>
    
    <form method="GET" style="margin:0; flex:2; min-width:350px;">
        <label style="display:block; margin-bottom: 8px; color: #9ca3af; font-size:14px;">全局搜索 (支持卡密/本站订单号/支付方交易号)</label>
        <div style="display:flex; gap:10px;">
            <input type="text" name="q" placeholder="输入搜索关键词..." value="<?php echo htmlspecialchars($searchQ); ?>" style="margin:0; flex:1; padding:10px; border:1px solid #475569; background:#0f172a; color:white; border-radius:4px;">
            <button type="submit" class="btn" style="background: #eab308; color: #000; font-weight: bold;">查询</button>
            <?php if($searchQ): ?><a href="asmrtap.php" class="btn" style="background: #4b5563;">清除</a><?php endif; ?>
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
    <button class="tab-btn" onclick="switchTab('tab-licenses')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">🔑 卡密管理</button>
    <button class="tab-btn" onclick="switchTab('tab-orders')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">💰 订单流水</button>
    <button class="tab-btn" onclick="switchTab('tab-logs')" style="padding: 10px 20px; background: transparent; color: #9ca3af; border: none; cursor: pointer; font-size: 16px;">📜 诊断日志</button>
</div>

<div id="tab-licenses" class="tab-content" style="display: none;">
<h3>许可密钥管理 (Licenses) <?php if($searchQ) echo "<span style='color:#38bdf8;font-size:16px;'>- 搜索结果</span>"; ?></h3>
<table>
    <tr>
        <th>ID</th>
        <th>激活码 (License Key)</th>
        <th>绑定机器数</th>
        <th>状态</th>
        <th>关联订单</th>
        <th>最后激活时间</th>
        <th>操作</th>
    </tr>
    <?php foreach ($licenses as $l): 
        $bound_machines = json_decode($l['machines_bound'], true) ?? [];
        $mc = count($bound_machines);
    ?>
    <tr>
        <td><?php echo $l['id']; ?></td>
        <td style="font-family: monospace; color:#fbbf24;"><?php echo $l['license_key']; ?></td>
        <td><?php echo $mc . ' / ' . $l['max_machines']; ?></td>
        <td><?php echo $l['status'] == 'active' ? '<span class="success">正常</span>' : '<span class="btn-danger">封禁</span>'; ?></td>
        <td><?php echo $l['order_no']; ?></td>
        <td><?php echo $l['last_activated'] ?: '从未激活'; ?></td>
        <td>
            <a href="?reset_id=<?php echo $l['id']; ?>" class="btn btn-warning" style="background:#f59e0b;color:white;text-decoration:none;" onclick="return confirm('确定解绑此卡密的所有机器吗？')">清空重置电脑</a>
            <a href="?del_license_id=<?php echo $l['id']; ?>" class="btn btn-danger" style="text-decoration:none; margin-left:8px;" onclick="return confirm('确定要彻底永久删除此卡密吗？本操作不可逆复原！\n如果此卡密已被客户激活，客户将会在下次强制断网核对时被封禁！')">彻底删除</a>
        </td>
    </tr>
    <?php endforeach; ?>
</table>
</div>

<div id="tab-orders" class="tab-content" style="display: none;">
<h3>最近订单 (Orders)</h3>
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

<div id="tab-users" class="tab-content">
<h3 style="color:#38bdf8;">用户终端大盘 (已安装联机设备心跳数据)</h3>
<table>
    <tr>
        <th>机器指纹 ID (Machine ID)</th>
        <th>当前状态 (Status)</th>
        <th>试用剩余时长 (Trial Limit)</th>
        <th>最后心跳时间 (Last Seen)</th>
        <th>远程操作</th>
    </tr>
    <?php foreach ($machines as $m): ?>
    <tr>
        <td style="font-family: monospace; color:#a7f3d0;"><?php echo htmlspecialchars($m['machine_id']); ?></td>
        <td style="font-weight:bold; color:<?php echo $m['status'] == 'active' ? '#10b981' : '#f59e0b'; ?>"><?php echo $m['status'] == 'active' ? '已激活' : '未激活(试用中)'; ?></td>
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
</script>

<div style="height:100px;"></div>

</body>
</html>
