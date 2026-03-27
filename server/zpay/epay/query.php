<?php
require_once("config.php");

$info=getbody("https://z-pay.cn/api.php?act=order&pid=$pid&key=$key&out_trade_no=20230506030508","","GET");
die($info);
?>