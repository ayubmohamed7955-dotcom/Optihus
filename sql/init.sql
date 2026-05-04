PRAGMA foreign_keys = ON;

-- All timestamps are stored as ISO-8601 UTC text (example: 2026-05-01T16:40:00Z).

CREATE TABLE IF NOT EXISTS telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    humidity_air REAL,
    humidity_soil REAL,
    pump_status TEXT NOT NULL,
    motor_status TEXT NOT NULL,
    error_status TEXT NOT NULL DEFAULT 'none',
    source TEXT NOT NULL DEFAULT 'miljoestyring'
);

CREATE INDEX IF NOT EXISTS idx_telemetry_timestamp ON telemetry(timestamp);

CREATE TABLE IF NOT EXISTS system_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    event_type TEXT NOT NULL,
    description TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT 'info'
);

CREATE INDEX IF NOT EXISTS idx_system_events_timestamp ON system_events(timestamp);

CREATE TABLE IF NOT EXISTS profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    crop_type TEXT NOT NULL,
    min_humidity_air REAL NOT NULL,
    max_humidity_air REAL NOT NULL,
    min_humidity_soil REAL NOT NULL,
    max_humidity_soil REAL NOT NULL,
    interval_sec INTEGER NOT NULL,
    is_default INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS active_config (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    timestamp TEXT NOT NULL,
    mode TEXT NOT NULL CHECK (mode IN ('auto', 'manual')),
    profile_name TEXT NOT NULL,
    interval_sec INTEGER NOT NULL,
    emergency_stop INTEGER NOT NULL DEFAULT 0 CHECK (emergency_stop IN (0, 1))
);

CREATE TABLE IF NOT EXISTS command_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    action TEXT NOT NULL,
    payload_json TEXT,
    source TEXT NOT NULL,
    status TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_command_history_timestamp ON command_history(timestamp);

INSERT INTO active_config (id, timestamp, mode, profile_name, interval_sec, emergency_stop)
SELECT 1, '1970-01-01T00:00:00Z', 'auto', 'tomato', 900, 0
WHERE NOT EXISTS (SELECT 1 FROM active_config WHERE id = 1);
