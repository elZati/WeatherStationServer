<?php
ini_set('log_errors', 1);
ini_set('error_log', 'error.log');
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$action = $_GET['action'] ?? '';

$conn = new mysqli('localhost', 'rxtxdesi_saa', 'Belg1a', 'rxtxdesi_weather');
if ($conn->connect_errno) {
    http_response_code(500);
    echo json_encode(['error' => 'DB connection failed']);
    exit;
}

if ($action === 'latest') {
    // Most recent row per node using MAX(id) join — efficient with index
    $sql = 'SELECT r.node_id, r.temp, r.hum, r.press, r.batt, r.timestamp
            FROM node_readings r
            INNER JOIN (
                SELECT node_id, MAX(id) AS max_id FROM node_readings GROUP BY node_id
            ) latest ON r.node_id = latest.node_id AND r.id = latest.max_id';
    $result = $conn->query($sql);
    $rows = [];
    while ($row = $result->fetch_assoc()) {
        $row['temp']  = (float)$row['temp'];
        $row['hum']   = (float)$row['hum'];
        $row['press'] = (float)$row['press'];
        $row['batt']  = (float)$row['batt'];
        $rows[] = $row;
    }
    echo json_encode($rows);

} elseif ($action === 'series') {
    $from = validDate($_GET['from'] ?? date('Y-m-d'));
    $to   = validDate($_GET['to']   ?? date('Y-m-d'));

    $stmt = $conn->prepare(
        'SELECT node_id, temp, hum, press, batt, timestamp AS ts
         FROM node_readings
         WHERE DATE(timestamp) BETWEEN ? AND ?
           AND temp IS NOT NULL AND temp != -99.9
         ORDER BY node_id, timestamp'
    );
    $stmt->bind_param('ss', $from, $to);
    $stmt->execute();
    $result = $stmt->get_result();

    $data = [];
    while ($row = $result->fetch_assoc()) {
        $nid = (int)$row['node_id'];
        $data[$nid][] = [
            'ts'    => $row['ts'],
            'temp'  => (float)$row['temp'],
            'hum'   => (float)$row['hum'],
            'press' => (float)$row['press'],
            'batt'  => (float)$row['batt'],
        ];
    }
    $stmt->close();
    echo json_encode((object)$data);

} elseif ($action === 'stats') {
    $from = validDate($_GET['from'] ?? date('Y-m-d'));
    $to   = validDate($_GET['to']   ?? date('Y-m-d'));

    $stmt = $conn->prepare(
        'SELECT node_id,
                MIN(temp)  AS min_temp,  MAX(temp)  AS max_temp,
                MIN(hum)   AS min_hum,   MAX(hum)   AS max_hum,
                MIN(press) AS min_press, MAX(press) AS max_press
         FROM node_readings
         WHERE DATE(timestamp) BETWEEN ? AND ?
           AND temp IS NOT NULL AND temp != -99.9
         GROUP BY node_id'
    );
    $stmt->bind_param('ss', $from, $to);
    $stmt->execute();
    $result = $stmt->get_result();

    $data = [];
    while ($row = $result->fetch_assoc()) {
        $nid = (int)$row['node_id'];
        $data[$nid] = [
            'min_temp'  => (float)$row['min_temp'],
            'max_temp'  => (float)$row['max_temp'],
            'min_hum'   => (float)$row['min_hum'],
            'max_hum'   => (float)$row['max_hum'],
            'min_press' => (float)$row['min_press'],
            'max_press' => (float)$row['max_press'],
        ];
    }
    $stmt->close();
    echo json_encode((object)$data);

} else {
    http_response_code(400);
    echo json_encode(['error' => 'Unknown action']);
}

$conn->close();

function validDate($s) {
    return preg_match('/^\d{4}-\d{2}-\d{2}$/', $s) ? $s : date('Y-m-d');
}
?>
