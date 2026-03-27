<?php
/**
 * UMC Ultra 高级授权 - 在线自助购买商店
 */
require_once 'config.php';

// 如果有支付回调的订单号参数，展示激活码
$orderNo = $_GET['order'] ?? '';
$autoCheck = false;
$keyToShow = '';

if ($orderNo) {
    $autoCheck = true;
    $pdo = getDb();
    $stmt = $pdo->prepare("SELECT status, license_key FROM orders WHERE out_trade_no = ?");
    $stmt->execute([$orderNo]);
    $order = $stmt->fetch();
    if ($order && $order['status'] === 'paid') {
        $keyToShow = $order['license_key'];
        $autoCheck = false;
    }
}
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>获得 UMC Ultra 终身高级版授权</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
        .card { background: #1e293b; padding: 40px; border-radius: 12px; box-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1), 0 2px 4px -2px rgb(0 0 0 / 0.1); max-width: 500px; width: 100%; border: 1px solid #334155; }
        h1 { margin-top: 0; color: #38bdf8; font-size: 24px; text-align: center; }
        p { color: #94a3b8; line-height: 1.6; }
        .price { font-size: 32px; font-weight: bold; color: #10b981; text-align: center; margin: 20px 0; }
        .features { list-style: none; padding: 0; margin-bottom: 30px; }
        .features li { padding-left: 24px; position: relative; margin-bottom: 12px; color: #cbd5e1; }
        .features li::before { content: "✓"; position: absolute; left: 0; color: #10b981; font-weight: bold; }
        .btn-wx { background: #059669; color: white; border: none; padding: 16px; border-radius: 8px; width: 48%; cursor: pointer; font-size: 16px; font-weight: bold; transition: background 0.3s; }
        .btn-wx:hover { background: #047857; }
        .btn-ali { background: #0ea5e9; color: white; border: none; padding: 16px; border-radius: 8px; width: 48%; cursor: pointer; font-size: 16px; font-weight: bold; transition: background 0.3s; }
        .btn-ali:hover { background: #0284c7; }
        .btn-group { display: flex; justify-content: space-between; margin-top: 15px; }
        .success-box { background: #064e3b; border: 2px solid #10b981; padding: 20px; border-radius: 8px; text-align: center; margin-top: 20px; }
        .license-code { background: #0f172a; padding: 15px; border-radius: 6px; font-family: monospace; font-size: 20px; color: #fbbf24; margin: 15px 0; letter-spacing: 1px; user-select: text;}
        .polling { text-align: center; padding: 30px; }
        .spinner { border: 4px solid rgba(255,255,255,0.1); border-left-color: #3b82f6; border-radius: 50%; width: 30px; height: 30px; animation: spin 1s linear infinite; margin: 0 auto 15px; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        /* erphpdown style QR Modal (True In-Page without Redirects) */
        .erphp-overlay { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.7); align-items: center; justify-content: center; backdrop-filter: blur(4px); }
        .erphp-box { background: #fff; width: 340px; border-radius: 6px; overflow: hidden; box-shadow: 0 10px 30px rgba(0,0,0,0.5); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; text-align: center; position: relative; animation: popin 0.3s cubic-bezier(0.18, 0.89, 0.32, 1.28); }
        @keyframes popin { 0% { opacity: 0; transform: scale(0.8); } 100% { opacity: 1; transform: scale(1); } }
        .erphp-close { position: absolute; right: 15px; top: 10px; font-size: 26px; color: #999; cursor: pointer; line-height: 1; transition: color 0.3s; }
        .erphp-close:hover { color: #333; }
        .erphp-header { padding: 30px 20px 20px; }
        .erphp-title-box { display: flex; align-items: center; justify-content: center; gap: 8px; font-size: 20px; color: #333; margin-bottom: 20px; font-weight: 500;}
        .erphp-title-box svg { width: 26px; height: 26px; }
        .erphp-sub { font-size: 14px; color: #888; margin-bottom: 12px; }
        .erphp-price { font-size: 36px; color: #333; font-weight: 400; margin-bottom: 10px; }
        .erphp-price small { font-size: 20px; margin-right: 3px; }
        .erphp-qr-box { width: 200px; height: 200px; margin: 0 auto; display: flex; justify-content: center; align-items: center; }
        /* The QR code canvas will be inside here */
        .erphp-footer { color: #fff; padding: 15px; font-size: 15px; margin-top: 20px; font-weight: 500; letter-spacing: 1px;}
    </style>
    <!-- 引入 qrcode.js -->
    <script src="https://cdn.bootcdn.net/ajax/libs/qrcodejs/1.0.0/qrcode.min.js"></script>
</head>
<body>

<div class="card">
    <h1>UMC Ultra By ASMRTOP (永久授权)</h1>
    
    <?php if ($keyToShow): ?>
        <!-- 支付成功状态 -->
        <div class="success-box">
            <h2 style="color: #34d399; margin-top:0;">支付成功，感谢购买！</h2>
            <p style="color: #a7f3d0;">这是您的激活码 (支持更换电脑激活)</p>
            <div class="license-code"><?php echo htmlspecialchars($keyToShow); ?></div>
            <p style="font-size: 14px;">请打开 DAW，在配置界面输入上述激活码<br><b>务必截图保存</b></p>
        </div>

    <?php elseif ($autoCheck): ?>
        <!-- 支付中, JS 轮询结果 -->
        <div class="polling" id="pollBox">
            <div class="spinner"></div>
            <h3 style="color: #cbd5e1;">正在等待支付结果归档...</h3>
            <p>如果您已经支付，请不要关闭此页面</p>
        </div>
        <script>
            let polling = setInterval(() => {
                fetch('check_order.php?order=<?php echo htmlspecialchars($orderNo); ?>')
                .then(r => r.json())
                .then(data => {
                    if(data.status === 'success') {
                        clearInterval(polling);
                        document.getElementById('pollBox').innerHTML = `
                            <div class="success-box">
                                <h2 style="color: #34d399; margin-top:0;">支付成功！</h2>
                                <p style="color: #a7f3d0;">您的激活码为：</p>
                                <div class="license-code">${data.license_key}</div>
                                <p style="font-size: 14px;">请打开 DAW 输入此激活码。(可绑定2台电脑)</p>
                            </div>
                        `;
                    }
                });
            }, 3000);
        </script>

    <?php else: ?>
        <div id="dynamicContent">
            <div class="price">¥ <?php echo LICENSE_PRICE; ?></div>
            
            <ul class="features">
                <li>完整解锁 UMC 接口所有虚拟双通道映射</li>
                <li>解除 60 分钟试用限制，一次购买终身有效</li>
                <li>支持更换电脑重新激活 (最高同时授权 2 台电脑)</li>
                <li>自动同步官方核心架构更新和系统适配</li>
                <li>激活后自动去除 ASIO 驱动列表后缀广告宣传语</li>
            </ul>

            <div class="btn-group">
                <button onclick="openPay('wxpay')" class="btn-wx">微信支付 (WeChat)</button>
                <button onclick="openPay('alipay')" class="btn-ali">支付宝 (Alipay)</button>
            </div>
            <p style="text-align: center; font-size: 12px; margin-top: 25px; color: #64748b;">
                点击购买将弹出防劫持收银台，支付成功后自动在此页出码
            </p>
        </div>
    <?php endif; ?>
</div>

<!-- 仿原版高端支付收款机弹窗 (完全原生局内渲染，0劫持风险) -->
<div id="qrModal" class="erphp-overlay">
    <div class="erphp-box">
        <span class="erphp-close" onclick="cancelPay()">&times;</span>
        <div class="erphp-header">
            <div class="erphp-title-box" id="qrTitleBox">
                <!-- SVG Icon and Text dynamically injected -->
            </div>
            <div class="erphp-sub">付费资源-ASMRTOP</div>
            <div class="erphp-price"><small>¥</small><?php echo LICENSE_PRICE; ?></div>
        </div>
        <div class="erphp-qr-box" id="qrcodeDOM">
            <!-- Loading Status or QR Code -->
            <div class="spinner" id="qrLoader" style="border-width:2px; width:20px;height:20px; border-left-color:#333;"></div>
        </div>
        <div class="erphp-footer" id="qrFooter">
            请扫码支付，支付成功后会自动发卡
        </div>
    </div>
</div>

<script>
let currentPolling = null;
let payWin = null;
let qrcodeObj = null;

const THEME_ALI = {
    color: '#00a0e9',
    svg: '<svg viewBox="0 0 1024 1024" fill="#00a0e9"><path d="M853.333333 0H170.666667C76.8 0 0 76.8 0 170.666667v682.666666C0 947.2 76.8 1024 170.666667 1024h682.666666C947.2 1024 1024 947.2 1024 853.333333V170.666667C1024 76.8 947.2 0 853.333333 0zM725.333333 580.266667c-51.2 110.933333-128 200.533333-221.866666 268.8-17.066667-21.333333-42.666667-46.933333-68.266667-64 76.8-51.2 145.066667-123.733333 183.466667-204.8H384c-12.8 0-21.333333-8.533333-21.333334-21.333334v-51.2c0-12.8 8.533333-21.333333 21.333334-21.333333h192V362.666667H332.8c-12.8 0-21.333333-8.533333-21.333333-21.333334v-51.2c0-12.8 8.533333-21.333333 21.333333-21.333333h243.2V192c0-12.8 8.533333-21.333333 21.333333-21.333333h59.733334c12.8 0 21.333333 8.533333 21.333333 21.333333v76.8h251.733334c12.8 0 21.333333 8.533333 21.333333 21.333333v51.2c0 12.8-8.533333 21.333333-21.333333 21.333334h-251.733334v123.733333h209.066667c12.8 0 21.333333 8.533333 21.333333 21.333333v51.2c0 12.8-4.266667 21.333333-17.066666 21.333334h-166.4z M320 537.6c59.733333 0 115.2 25.6 153.6 68.266667-17.066667 17.066667-46.933333 46.933333-68.266667 64-25.6-29.866667-64-46.933333-102.4-46.933334-85.333333 0-149.333333 72.533333-149.333333 149.333334 0 76.8 64 149.333333 149.333333 149.333333 46.933333 0 93.866667-21.333333 128-59.733333 17.066667 17.066667 46.933333 46.933333 68.266667 64C452.266667 972.8 388.266667 1002.666667 320 1002.666667c-132.266667 0-234.666667-106.666667-234.666667-234.666667S187.733333 537.6 320 537.6z m38.4 200.533333c-21.333333-21.333333-46.933333-34.133333-76.8-34.133333-42.666667 0-72.533333 34.133333-72.533333 72.533333 0 42.666667 29.866667 72.533333 72.533333 72.533334 34.133333 0 68.266667-21.333333 85.333333-51.2-4.266667-21.333333-8.533333-42.666667-8.533333-59.733334z"></path></svg>',
    text: '支付宝'
};
const THEME_WX = {
    color: '#07c160',
    svg: '<svg viewBox="0 0 1024 1024" fill="#07c160"><path d="M682.666667 256c-187.733333 0-341.333333 128-341.333334 298.666667 0 170.666667 153.6 298.666667 341.333334 298.666666 42.666667 0 85.333333-8.533333 119.466666-21.333333l93.866667 46.933333-25.6-89.6C930.133333 738.133333 981.333333 652.8 981.333333 554.666667c0-170.666667-153.6-298.666667-341.333333-298.666667L682.666667 256zM810.666667 426.666667c17.066667 0 34.133333 17.066667 34.133333 34.133333S827.733333 494.933333 810.666667 494.933333 776.533333 477.866667 776.533333 460.8s17.066667-34.133333 34.133334-34.133333zM554.666667 426.666667c17.066667 0 34.133333 17.066667 34.133333 34.133333s-17.066667 34.133333-34.133333 34.133333S520.533333 477.866667 520.533333 460.8s17.066667-34.133333 34.133334-34.133333zM384 128C170.666667 128 0 268.8 0 448c0 102.4 51.2 196.266667 136.533333 260.266667l-34.133333 106.666666L234.666667 768C281.6 785.066667 328.533333 797.866667 384 797.866667v-42.666667C196.266667 755.2 42.666667 618.666667 42.666667 448c0-170.666667 153.6-307.2 341.333333-307.2S725.333333 277.333333 725.333333 448v72.533333zM256 311.466667c21.333333 0 42.666667 17.066667 42.666667 42.666666S277.333333 396.8 256 396.8s-42.666667-17.066667-42.666667-42.666667S234.666667 311.466667 256 311.466667zM512 311.466667c21.333333 0 42.666667 17.066667 42.666667 42.666666s-17.066667 42.666667-42.666667 42.666667-42.666667-17.066667-42.666667-42.666667S490.666667 311.466667 512 311.466667z"></path></svg>',
    text: '微信支付'
};

function openPay(type) {
    const t = (type === 'alipay') ? THEME_ALI : THEME_WX;
    document.getElementById('qrTitleBox').innerHTML = t.svg + '<span>' + t.text + '</span>';
    document.getElementById('qrTitleBox').style.color = t.color;
    document.getElementById('qrFooter').style.background = t.color;
    
    // Clear old QR & show loader
    const dom = document.getElementById('qrcodeDOM');
    dom.innerHTML = '<div class="spinner" id="qrLoader" style="border-width:2px; width:20px;height:20px; border-left-color:' + t.color + ';"></div>';

    document.getElementById('qrModal').style.display = 'flex';

    fetch('buy.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'X-Requested-With': 'XMLHttpRequest' },
        body: 'type=' + type
    })
    .then(r => r.json())
    .then(data => {
        if(data.status === 'ok') {
            // Render the raw QR String via local canvas, 0 jumping
            dom.innerHTML = ''; // clear loader
            if(data.qr_text.startsWith('http')) {
                // Return string was actually an HTTP jump link / EasyPay gateway payload
                // Very rare, but if they strictly return a wrapper link:
                const qr = new QRCode(dom, { text: data.qr_text, width: 200, height: 200 });
            } else {
                // Clean scheme string (alipays:// or weixin://)
                const qr = new QRCode(dom, { text: data.qr_text, width: 200, height: 200 });
            }
            startPolling(data.order_no);
        } else if (data.status === 'fallback') {
            // IF ZPay blocks MAPI, we gracefully fallback to the window popup
            cancelPay(); 
            const left = (screen.width - 650) / 2; const top = (screen.height - 700) / 2;
            payWin = window.open(data.pay_url, '_blank', `width=650,height=700,top=${top},left=${left}`);
            document.body.insertAdjacentHTML('beforeend', '<div id="fkModal" class="erphp-overlay" style="display:flex;"><div class="erphp-box"><h3 style="margin:20px">浏览器窗口支付中</h3><p style="color:#666">请在新窗口完成付款...</p></div></div>');
            startPolling(data.order_no);
        } else {
            alert('订单生成失败: ' + (data.msg || ''));
            cancelPay();
        }
    })
    .catch(e => {
        alert('网络错误，请重试: ' + e);
        cancelPay();
    });
}

function cancelPay() {
    document.getElementById('qrModal').style.display = 'none';
    if(currentPolling) {
        clearInterval(currentPolling);
        currentPolling = null;
    }
}

function startPolling(orderNo) {
    if(currentPolling) clearInterval(currentPolling);
    currentPolling = setInterval(() => {
        fetch('check_order.php?order=' + orderNo)
        .then(r => r.json())
        .then(data => {
            if(data.status === 'success') {
                // 订单处理完毕，完美扫尾！
                clearInterval(currentPolling);
                currentPolling = null;
                document.getElementById('qrModal').style.display = 'none';
                let fk = document.getElementById('fkModal'); if(fk) fk.remove();
                if(payWin && !payWin.closed) payWin.close();
                
                // 替换主界面为成功视图
                document.getElementById('dynamicContent').innerHTML = `
                    <div class="success-box">
                        <h2 style="color: #34d399; margin-top:0;">支付成功，感谢购买！</h2>
                        <p style="color: #a7f3d0;">这是您的永久激活码</p>
                        <div class="license-code">${data.license_key}</div>
                        <p style="font-size: 14px;">请打开 DAW 宿主并在 ASIO 控制面板填入。(最高授权2台电脑)</p>
                    </div>
                `;
            }
        });
    }, 3000); // 3秒轮询一次
}
</script>

</body>
</html>
