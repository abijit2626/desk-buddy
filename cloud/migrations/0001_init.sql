-- Dewy cloud: devices, the phones that control them, and things waiting to be delivered.

CREATE TABLE devices (
  id            TEXT PRIMARY KEY,
  token_hash    TEXT NOT NULL UNIQUE,      -- SHA-256 of the device's secret token
  created_at    INTEGER NOT NULL,
  last_seen     INTEGER,
  fw_version    TEXT,
  pair_code     TEXT UNIQUE,               -- shown on Dewy's screen while pairing a phone
  pair_expires  INTEGER,
  settings      TEXT NOT NULL,             -- JSON, checked by settings.ts before saving
  settings_rev  INTEGER NOT NULL DEFAULT 1
);

-- Phones and links. role 'owner' can change everything; role 'notes' can only send notes.
CREATE TABLE app_tokens (
  id          TEXT PRIMARY KEY,
  token_hash  TEXT NOT NULL UNIQUE,
  device_id   TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  role        TEXT NOT NULL CHECK (role IN ('owner', 'notes')),
  label       TEXT NOT NULL,
  created_at  INTEGER NOT NULL,
  last_used   INTEGER
);
CREATE INDEX app_tokens_device ON app_tokens(device_id);

-- Things for Dewy to do on its next check-in: notes, study-timer start/stop.
CREATE TABLE events (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id     TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  kind          TEXT NOT NULL,
  body          TEXT NOT NULL,
  created_at    INTEGER NOT NULL,
  delivered_at  INTEGER
);
CREATE INDEX events_pending ON events(device_id, delivered_at, id);

-- Daily counters for limits (notes sent, AI chats).
CREATE TABLE usage (
  device_id  TEXT NOT NULL,
  day        TEXT NOT NULL,
  kind       TEXT NOT NULL,
  count      INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (device_id, day, kind)
);

-- The newest firmware, offered to every Dewy that runs an older version.
CREATE TABLE firmware (
  id          INTEGER PRIMARY KEY CHECK (id = 1),
  version     TEXT NOT NULL,
  url         TEXT NOT NULL,
  sha256      TEXT NOT NULL,
  size        INTEGER NOT NULL,
  updated_at  INTEGER NOT NULL
);
