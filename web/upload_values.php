<?php
ini_set('log_errors', 1);
ini_set('error_log', 'error.log');

$node_id = isset($_GET['node_id']) ? (int)$_GET['node_id'] : 0;
$temp    = isset($_GET['temp'])    ? (float)$_GET['temp']  : null;
$hum     = isset($_GET['hum'])     ? (float)$_GET['hum']   : null;
$press   = isset($_GET['press'])   ? (float)$_GET['press'] : null;
$batt    = isset($_GET['batt'])    ? (float)$_GET['batt']  : null;

if ($node_id < 1 || $node_id > 5) {
    http_response_code(400);
    exit('Invalid node_id');
}

$conn = new mysqli('localhost', 'rxtxdesi_saa', 'Belg1a', 'rxtxdesi_weather');
if ($conn->connect_errno) {
    http_response_code(500);
    exit;
}

$stmt = $conn->prepare(
    'INSERT INTO node_readings (node_id, temp, hum, press, batt) VALUES (?,?,?,?,?)'
);
$stmt->bind_param('idddd', $node_id, $temp, $hum, $press, $batt);
$stmt->execute();
$stmt->close();
$conn->close();

echo 'OK';
?>
