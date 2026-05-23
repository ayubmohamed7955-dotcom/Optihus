-- Uses ISO-8601 UTC timestamps.
INSERT OR IGNORE INTO profiles (
    name,
    min_humidity_air, max_humidity_air,
    min_humidity_soil, max_humidity_soil,
    min_temp_c, max_temp_c,
    interval_sec, is_default, created_at, updated_at
) VALUES
('tomat', 60.0, 70.0, 35.0, 60.0, 20.0, 25.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'),
('agurk', 70.0, 80.0, 45.0, 70.0, 22.0, 25.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'),
('peberfrugt', 60.0, 75.0, 30.0, 55.0, 22.0, 24.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');
