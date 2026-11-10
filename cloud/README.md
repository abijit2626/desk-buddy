# Dewy cloud

A small Cloudflare Worker that does three jobs:

- **Hosts the phone app** (`app/`): a home-screen web app for iPhone with the study timer, notes, alarms,
  reminders and settings, plus a send-only notes page (`/notes`).
- **Keeps Dewy in sync**: settings are stored in a D1 database; notes and timer commands wait in a queue
  until Dewy checks in (every few seconds over HTTPS).
- **Offers firmware updates**, so Dewy can update itself from anywhere.

Everything fits in Cloudflare's free plan.

## How the pieces talk

```
her iPhone (app)  --HTTPS-->  Worker  <--HTTPS, every few seconds--  Dewy (ESP32)
your phone (notes link) ----^   |
                              D1 database
```

- **Dewy** registers once with a register key that only your firmware knows, and gets its own secret token.
- **Pairing**: Dewy shows an 8-character code on its screen; the app sends it and gets a token for that phone.
- **Notes links** carry a token after the `#`. They can only send notes, and can be removed in the app.
- All tokens are stored only as SHA-256 hashes. Every field a phone sends is checked before it's saved.

## One-time setup (about 10 minutes)

1. Make a free account at [cloudflare.com](https://dash.cloudflare.com/sign-up).
2. In this folder:
   ```sh
   npm install
   npx wrangler login
   npx wrangler d1 create dewy
   ```
   Copy the `database_id` it prints into `wrangler.toml`.
3. Create the tables: `npm run db:remote`
4. Make two long random secrets and store them in Cloudflare (it asks you to paste each one):
   ```sh
   npx wrangler secret put REGISTER_KEY
   npx wrangler secret put ADMIN_KEY
   ```
   A quick way to make one: `node -e "console.log(require('crypto').randomBytes(24).toString('base64url'))"`
5. Deploy: `npm run deploy`. It prints the address, like `https://dewy.<you>.workers.dev`.
6. In `firmware/dewy/secrets.h`, set `DEWY_SERVER` to that address and `DEWY_REGISTER_KEY` to the same
   register key. Upload the firmware.
7. On her iPhone: open the address in Safari, tap Share, then **Add to Home Screen**. Open Dewy from the home
   screen and type the code Dewy shows.

## Working on it locally

```sh
cp .dev.vars.example .dev.vars     # local-only keys
npm run db:local                   # local database
npm run dev                        # http://127.0.0.1:8787
npm test                           # 40 end-to-end API checks (with dev running)
npm run check                      # TypeScript
```

## Publishing a firmware update

1. Bump `DEWY_VERSION` in `firmware/dewy/config.h` and build: Arduino IDE, Sketch, Export Compiled Binary.
2. Make a GitHub release (for example `v1.1.0`) and attach the `.bin` file.
3. Run `scripts/publish-firmware.mjs` (instructions at the top of the file). It checks the download matches
   your file, then every Dewy on an older version downloads it, checks its SHA-256, installs it and restarts.

## API

| Method | Path | Who | What |
|---|---|---|---|
| POST | `/api/device/register` | Dewy (register key) | First start: get a device token |
| POST | `/api/device/pair` | Dewy | Get a pairing code to show on screen |
| GET | `/api/device/sync?rev=&ack=` | Dewy | Settings (if changed), queued events, firmware offer |
| POST | `/api/pair` | anyone with a code | Pair a phone |
| GET | `/api/me` | phone or link | Who am I, is Dewy online |
| GET/PUT | `/api/settings` | phone | Read or save settings |
| POST | `/api/timer`, `/api/timer/stop` | phone | Study timer |
| GET/POST | `/api/notes` | phone or link | Notes |
| POST | `/api/links` | phone | Make a notes link |
| GET | `/api/access` | phone | Paired phones and links |
| DELETE | `/api/access/:id` | phone (or a link removing itself) | Remove one |
| POST | `/api/invite` | phone | Pairing code for another phone |
| PUT | `/api/admin/firmware` | admin key | Publish new firmware |
