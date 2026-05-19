PRAGMA foreign_keys = ON;

-- All timestamps are stored as ISO-8601 UTC text (example: 2026-05-01T16:40:00Z).

CREATE TABLE IF NOT EXISTS telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    temperature_c REAL,
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
    min_temp_c REAL NOT NULL DEFAULT 18,
    max_temp_c REAL NOT NULL DEFAULT 26,
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
    emergency_stop INTEGER NOT NULL DEFAULT 0 CHECK (emergency_stop IN (0, 1)),
    ventilation_enabled INTEGER NOT NULL DEFAULT 1 CHECK (ventilation_enabled IN (0, 1)),
    watering_enabled INTEGER NOT NULL DEFAULT 1 CHECK (watering_enabled IN (0, 1))
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

CREATE TABLE IF NOT EXISTS plant_boxes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label TEXT NOT NULL,
    slot INTEGER NOT NULL UNIQUE,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    min_soil_pct REAL NOT NULL DEFAULT -1,
    max_soil_pct REAL NOT NULL DEFAULT 100,
    pump_duration_sec INTEGER NOT NULL DEFAULT 1,
    hysteresis_sec INTEGER NOT NULL DEFAULT -1,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_plant_boxes_slot ON plant_boxes(slot);

INSERT INTO active_config (id, timestamp, mode, profile_name, interval_sec, emergency_stop)
SELECT 1, '1970-01-01T00:00:00Z', 'auto', 'tomato', 60, 0
WHERE NOT EXISTS (SELECT 1 FROM active_config WHERE id = 1);

INSERT INTO plant_boxes (label, slot, is_active, created_at, updated_at)
SELECT 'Plantekasse 1', 1, 0, '1970-01-01T00:00:00Z', '1970-01-01T00:00:00Z'
WHERE NOT EXISTS (SELECT 1 FROM plant_boxes);
