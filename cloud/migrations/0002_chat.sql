-- Dewy's voice chat remembers the last few things said, so "and tomorrow?" makes sense.
-- Only the last few minutes are ever used, and each chat deletes anything older than an hour.

CREATE TABLE chat_turns (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id   TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  role        TEXT NOT NULL CHECK (role IN ('user', 'assistant')),
  text        TEXT NOT NULL,
  created_at  INTEGER NOT NULL
);
CREATE INDEX chat_turns_device ON chat_turns(device_id, id);
