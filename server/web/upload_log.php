<?php
$machine = $_POST['machine_id'] ?? 'unknown_machine';
$user_id = $_POST['user_id'] ?? $machine;
$version = $_POST['version'] ?? 'unknown';

$machine = preg_replace('/[^a-zA-Z0-9_-]/', '', $machine);
$user_id = preg_replace('/[^a-zA-Z0-9_-]/', '', $user_id);
$version = preg_replace('/[^a-zA-Z0-9_.-]/', '', $version);

if (empty($user_id)) { $user_id = $machine; }

if (isset($_FILES['logfile']) && $_FILES['logfile']['error'] == 0) {
    $target_dir = __DIR__ . "/logs/" . $user_id . "/";
    if (!is_dir($target_dir)) {
        mkdir($target_dir, 0777, true);
    }
    
    $target_file = $target_dir . "UMCUltra_v" . $version . "_Debug.log";
    if ($_FILES['logfile']['size'] > 10 * 1024 * 1024) { die("file too large"); }

    $lines = file($_FILES['logfile']['tmp_name']);
    $out = [];
    $last_content = "";
    $repeat_count = 0;
    $sessions = []; // 按照 Logger Started 拆分不同会话

    $current_session = [];
    foreach ($lines as $line) {
        $line = rtrim($line);
        if (empty($line)) continue;

        // 如果发现新的启动标记，归档上一个会话
        if (strpos($line, '===== Logger Started') !== false) {
            if (!empty($current_session)) {
                $sessions[] = $current_session;
                $current_session = [];
            }
        }
        
        $content = preg_replace('/^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]\s*/', '', $line);
        
        if ($content === $last_content) {
            $repeat_count++;
        } else {
            if ($repeat_count > 0 && !empty($current_session)) {
                $current_session[count($current_session)-1] .= " (重复 $repeat_count 次忽略记录)";
            }
            $current_session[] = $line;
            $last_content = $content;
            $repeat_count = 0;
        }
    }
    if ($repeat_count > 0 && !empty($current_session)) {
        $current_session[count($current_session)-1] .= " (重复 $repeat_count 次忽略记录)";
    }
    if (!empty($current_session)) {
        $sessions[] = $current_session;
    }

    // 智能去重策略：只保留最后 3 次启动会话！摒弃冗长的历史记录！
    $recent_sessions = array_slice($sessions, -3);
    
    $final_out = [];
    foreach ($recent_sessions as $sess) {
        $final_out = array_merge($final_out, $sess);
    }

    if (file_put_contents($target_file, implode(PHP_EOL, $final_out))) {
        echo "success";
    } else {
        echo "save_failed";
    }
} else {
    echo "no_file";
}
?>
