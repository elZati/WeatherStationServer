<?php
ini_set('log_errors', 1);
ini_set('error_log', 'error.log');

$raw   = file_get_contents('php://input');
$nodes = json_decode($raw, true);

if (!is_array($nodes) || count($nodes) === 0) {
    http_response_code(400);
    exit('Bad request');
}

$conn = new mysqli('localhost', 'rxtxdesi_saa', 'Belg1a', 'rxtxdesi_weather');
if ($conn->connect_errno) {
    http_response_code(500);
    exit;
}

$stmt = $conn->prepare(
    'INSERT INTO node_readings (node_id, temp, hum, press, batt) VALUES (?,?,?,?,?)'
);

foreach ($nodes as $n) {
    $node_id = isset($n['node_id']) ? (int)$n['node_id'] : 0;
    if ($node_id < 1 || $node_id > 5) continue;
    $temp  = isset($n['temp'])  ? (float)$n['temp']  : null;
    $hum   = isset($n['hum'])   ? (float)$n['hum']   : null;
    $press = isset($n['press']) ? (float)$n['press'] : null;
    $batt  = isset($n['batt'])  ? (float)$n['batt']  : null;
    $stmt->bind_param('idddd', $node_id, $temp, $hum, $press, $batt);
    $stmt->execute();
}

$stmt->close();
$conn->close();

echo 'OK';
?>
