-- Uses ISO-8601 UTC timestamps.
INSERT OR IGNORE INTO profiles (
    name, crop_type,
    min_humidity_air, max_humidity_air,
    min_humidity_soil, max_humidity_soil,
    interval_sec, is_default, created_at, updated_at
) VALUES
('tomato', 'tomato', 55.0, 75.0, 35.0, 60.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'),
('cucumber', 'cucumber', 60.0, 85.0, 45.0, 70.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'),
('chili', 'chili', 50.0, 70.0, 30.0, 55.0, 60, 1, '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');
