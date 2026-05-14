<?php
ini_set("log_errors", 1);
ini_set("error_log", __DIR__ . "/app.log");
header("Content-Type: application/json");
header("Access-Control-Allow-Origin: *");

function saa_log($msg) {
    $ts = date("Y-m-d H:i:s");
    $ip = $_SERVER["REMOTE_ADDR"] ?? "-";
    file_put_contents(__DIR__ . "/app.log", "[$ts] [fetch] [$ip] $msg\n", FILE_APPEND | LOCK_EX);
}

$action = $_GET["action"] ?? "";
saa_log("action=$action " . http_build_query($_GET));

$conn = new mysqli("localhost", "rxtxdesi_saa", "Belg1a", "rxtxdesi_weather");
if ($conn->connect_errno) {
    saa_log("DB connect failed: " . $conn->connect_error);
    http_response_code(500);
    echo json_encode(["error" => "DB connection failed"]);
    exit;
}

if ($action === "latest") {
    $sql = "SELECT r.node_id, r.temp, r.hum, r.press, r.batt, r.eco2, r.tvoc, r.aqi, r.timestamp
            FROM node_readings r
            INNER JOIN (
                SELECT node_id, MAX(id) AS max_id FROM node_readings GROUP BY node_id
            ) latest ON r.node_id = latest.node_id AND r.id = latest.max_id";
    $result = $conn->query($sql);
    $rows = [];
    while ($row = $result->fetch_assoc()) {
        $row["temp"]  = (float)$row["temp"];
        $row["hum"]   = (float)$row["hum"];
        $row["press"] = (float)$row["press"];
        $row["batt"]  = (float)$row["batt"];
        if ($row["eco2"] !== null) $row["eco2"] = (int)$row["eco2"];
        if ($row["tvoc"] !== null) $row["tvoc"] = (int)$row["tvoc"];
        if ($row["aqi"]  !== null) $row["aqi"]  = (int)$row["aqi"];
        $rows[] = $row;
    }
    saa_log("latest => " . count($rows) . " node(s)");
    echo json_encode($rows);

} elseif ($action === "series") {
    $from = validDate($_GET["from"] ?? date("Y-m-d"));
    $to   = validDate($_GET["to"]   ?? date("Y-m-d"));
    $stmt = $conn->prepare(
        "SELECT node_id, temp, hum, press, batt, eco2, tvoc, aqi, timestamp AS ts
         FROM node_readings
         WHERE DATE(timestamp) BETWEEN ? AND ?
           AND temp IS NOT NULL AND temp != -99.9
         ORDER BY node_id, timestamp"
    );
    $stmt->bind_param("ss", $from, $to);
    $stmt->execute();
    $result = $stmt->get_result();
    $data = [];
    $count = 0;
    while ($row = $result->fetch_assoc()) {
        $nid = (int)$row["node_id"];
        $entry = [
            "ts"    => $row["ts"],
            "temp"  => (float)$row["temp"],
            "hum"   => (float)$row["hum"],
            "press" => (float)$row["press"],
            "batt"  => (float)$row["batt"],
        ];
        if ($row["eco2"] !== null) {
            $entry["eco2"] = (int)$row["eco2"];
            $entry["tvoc"] = (int)$row["tvoc"];
            $entry["aqi"]  = (int)$row["aqi"];
        }
        $data[$nid][] = $entry;
        $count++;
    }
    $stmt->close();
    saa_log("series from=$from to=$to => $count rows");
    echo json_encode((object)$data);

} elseif ($action === "stats") {
    $from = validDate($_GET["from"] ?? date("Y-m-d"));
    $to   = validDate($_GET["to"]   ?? date("Y-m-d"));
    $stmt = $conn->prepare(
        "SELECT node_id,
                MIN(temp)  AS min_temp,  MAX(temp)  AS max_temp,
                MIN(hum)   AS min_hum,   MAX(hum)   AS max_hum,
                MIN(press) AS min_press, MAX(press) AS max_press,
                MIN(eco2)  AS min_eco2,  MAX(eco2)  AS max_eco2
         FROM node_readings
         WHERE DATE(timestamp) BETWEEN ? AND ?
           AND temp IS NOT NULL AND temp != -99.9
         GROUP BY node_id"
    );
    $stmt->bind_param("ss", $from, $to);
    $stmt->execute();
    $result = $stmt->get_result();
    $data = [];
    while ($row = $result->fetch_assoc()) {
        $nid = (int)$row["node_id"];
        $entry = [
            "min_temp"  => (float)$row["min_temp"],
            "max_temp"  => (float)$row["max_temp"],
            "min_hum"   => (float)$row["min_hum"],
            "max_hum"   => (float)$row["max_hum"],
            "min_press" => (float)$row["min_press"],
            "max_press" => (float)$row["max_press"],
        ];
        if ($row["min_eco2"] !== null) {
            $entry["min_eco2"] = (int)$row["min_eco2"];
            $entry["max_eco2"] = (int)$row["max_eco2"];
        }
        $data[$nid] = $entry;
    }
    $stmt->close();
    saa_log("stats from=$from to=$to => " . count($data) . " node(s)");
    echo json_encode((object)$data);

} else {
    saa_log("Unknown action: $action");
    http_response_code(400);
    echo json_encode(["error" => "Unknown action"]);
}

$conn->close();

function validDate($s) {
    return preg_match("/^\d{4}-\d{2}-\d{2}$/", $s) ? $s : date("Y-m-d");
}
?>
