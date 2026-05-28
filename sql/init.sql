PRAGMA foreign_keys = ON;



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
    is_active INTEGER NOT NULL DEFAULT 0 CHECK (is_active IN (0, 1)),
    is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),
    min_soil_pct REAL NOT NULL DEFAULT 20,
    hysteresis_sec INTEGER NOT NULL DEFAULT 30,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_plant_boxes_slot ON plant_boxes(slot);

INSERT INTO active_config (
    id, timestamp, mode, profile_name, interval_sec, emergency_stop,
    ventilation_enabled, watering_enabled
)
SELECT 1, '1970-01-01T00:00:00Z', 'auto', '', 60, 0, 0, 0
WHERE NOT EXISTS (SELECT 1 FROM active_config WHERE id = 1);

INSERT OR IGNORE INTO profiles (
    name,
    min_humidity_air, max_humidity_air, min_humidity_soil, max_humidity_soil,
    min_temp_c, max_temp_c, interval_sec, is_default, created_at, updated_at
) VALUES
    ('tomat', 60.0, 70.0, 35.0, 60.0, 20.0, 25.0, 60, 1, '1970-01-01T00:00:00Z', '1970-01-01T00:00:00Z'),
    ('agurk', 70.0, 80.0, 45.0, 70.0, 22.0, 25.0, 60, 1, '1970-01-01T00:00:00Z', '1970-01-01T00:00:00Z'),
    ('peberfrugt', 60.0, 75.0, 30.0, 55.0, 22.0, 24.0, 60, 1, '1970-01-01T00:00:00Z', '1970-01-01T00:00:00Z');

INSERT INTO plant_boxes (
    label, slot, is_active, is_default, min_soil_pct,
    hysteresis_sec, created_at, updated_at
)
SELECT 'Plantekasse 1', 1, 0, 1, 20, 30, '1970-01-01T00:00:00Z', '1970-01-01T00:00:00Z'
WHERE NOT EXISTS (SELECT 1 FROM plant_boxes);
