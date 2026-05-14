<?php
ini_set("log_errors", 1);
ini_set("error_log", __DIR__ . "/app.log");

function saa_log($msg) {
    $ts = date("Y-m-d H:i:s");
    $ip = $_SERVER["REMOTE_ADDR"] ?? "-";
    file_put_contents(__DIR__ . "/app.log", "[$ts] [upload] [$ip] $msg\n", FILE_APPEND | LOCK_EX);
}

$raw   = file_get_contents("php://input");
$nodes = json_decode($raw, true);

if (!is_array($nodes) || count($nodes) === 0) {
    saa_log("Bad request — body: " . substr($raw, 0, 200));
    http_response_code(400);
    exit("Bad request");
}

saa_log("Received " . count($nodes) . " node(s)");

$conn = new mysqli("localhost", "rxtxdesi_saa", "Belg1a", "rxtxdesi_weather");
if ($conn->connect_errno) {
    saa_log("DB connect failed: " . $conn->connect_error);
    http_response_code(500);
    exit;
}

$stmt = $conn->prepare(
    "INSERT INTO node_readings (node_id, temp, hum, press, batt, eco2, tvoc, aqi)
     VALUES (?,?,?,?,?,?,?,?)"
);

foreach ($nodes as $n) {
    $node_id = isset($n["node_id"]) ? (int)$n["node_id"] : 0;
    if ($node_id < 1 || $node_id > 5) {
        saa_log("Skipped invalid node_id=" . $node_id);
        continue;
    }
    $temp  = isset($n["temp"])  ? (float)$n["temp"]  : null;
    $hum   = isset($n["hum"])   ? (float)$n["hum"]   : null;
    $press = isset($n["press"]) ? (float)$n["press"] : null;
    $batt  = isset($n["batt"])  ? (float)$n["batt"]  : null;
    $eco2  = isset($n["eco2"])  ? (int)$n["eco2"]    : null;
    $tvoc  = isset($n["tvoc"])  ? (int)$n["tvoc"]    : null;
    $aqi   = isset($n["aqi"])   ? (int)$n["aqi"]     : null;
    $stmt->bind_param("iddddiii", $node_id, $temp, $hum, $press, $batt, $eco2, $tvoc, $aqi);
    if ($stmt->execute()) {
        $hw = ($eco2 !== null) ? "HW2" : "HW1";
        saa_log("  node $node_id [$hw] T=$temp H=$hum P=$press B=$batt" .
                ($eco2 !== null ? " eCO2=$eco2 TVOC=$tvoc AQI=$aqi" : ""));
    } else {
        saa_log("  node $node_id INSERT failed: " . $stmt->error);
    }
}

$stmt->close();
$conn->close();
saa_log("Done");
echo "OK";
?>
