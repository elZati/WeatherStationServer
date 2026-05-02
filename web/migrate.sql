-- Run this once in cPanel phpMyAdmin to create the new per-node table.
-- The old "measurement" table is left untouched.

CREATE TABLE IF NOT EXISTS node_readings (
  id        INT AUTO_INCREMENT PRIMARY KEY,
  node_id   TINYINT UNSIGNED NOT NULL,
  temp      FLOAT,
  hum       FLOAT,
  press     FLOAT,
  batt      FLOAT,
  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_node_time (node_id, timestamp),
  INDEX idx_time (timestamp)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
