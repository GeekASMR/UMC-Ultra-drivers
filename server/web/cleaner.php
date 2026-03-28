<?php
$files = glob(__DIR__ . '/logs/*.log');
$count = 0;
foreach($files as $f) {
    if(is_file($f)) {
        unlink($f);
        $count++;
    }
}
echo "Cleaned $count legacy flat log files!";
?>
