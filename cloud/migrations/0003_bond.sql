-- Dewy reports how close its bond with its person is (0-1000), so the phone app can show it.
ALTER TABLE devices ADD COLUMN bond INTEGER;
