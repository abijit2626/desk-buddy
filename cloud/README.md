# Dewy cloud

A small Cloudflare Worker that does four jobs:

- **Hosts the phone app** (`app/`): a home-screen web app for iPhone with the study timer, notes, alarms,
  reminders and settings, plus a send-only notes page (`/notes`).
- **Keeps Dewy in sync**: settings are stored in a D1 database; notes and timer commands wait in a queue
  until Dewy checks in (every few seconds over HTTPS).
- **Offers firmware updates**, so Dewy can update itself from anywhere.
- **Answers Dewy's spoken questions** ("Hey Dewy, what's the news?"), see [Voice chat](#voice-chat).

The first three fit in Cloudflare's free plan. Voice chat costs a little (see below).

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
5. Voice chat needs nothing more: its thinking step runs on Cloudflare's free allowance. (To use Claude instead,
   see [Voice chat](#voice-chat).)
6. Deploy: `npm run deploy`. It prints the address, like `https://dewy.<you>.workers.dev`.
7. In `firmware/dewy/secrets.h`, set `DEWY_SERVER` to that address and `DEWY_REGISTER_KEY` to the same
   register key. Upload the firmware.
8. On her iPhone: open the address in Safari, tap Share, then **Add to Home Screen**. Open Dewy from the home
   screen and type the code Dewy shows.

## Working on it locally

```sh
cp .dev.vars.example .dev.vars     # local-only keys
npm run db:local                   # local database
npm run dev                        # http://127.0.0.1:8787
npm test                           # 69 end-to-end API checks (with dev running)
npm run check                      # TypeScript
```

`npm run dev` uses `wrangler.dev.toml`, which is `wrangler.toml` without the `[ai]` line (Workers AI only exists
online and would ask for a Cloudflare login). `.dev.vars` sets `CHAT_FAKE_AI="1"`, so voice chat runs on scripted
stand-ins: no account, no key, no cost. Keep the two toml files in step when you change one.

## Voice chat

```
Dewy mic --16 kHz audio--> /api/device/chat --> Whisper (words) --> chat model (+ tools) --> Aura (voice) --WAV--> Dewy speaker
```

- **Request**: `POST /api/device/chat` with Dewy's token. Body is raw 16 kHz, 16-bit, mono audio
  (`audio/pcm`, 0.4 to 12 seconds), or `{"text": "..."}` as JSON for typing and tests.
- **Answer**: a WAV file. `x-dewy-heard` and `x-dewy-reply` headers (URL-encoded) carry the words. If the day's
  speaking budget is used up, or the voice service fails, the answer is JSON `{heard, reply, audio:false}` instead.
  The WAV's length field may be unreliable: read the header, then play until the data ends.
- **The chat model** is set by `CHAT_PROVIDER` in `wrangler.toml`:
  - `"workers-ai"` (default): an open model on Cloudflare (`@cf/meta/llama-3.3-70b-instruct-fp8-fast`, change it
    with `WORKERS_AI_MODEL`). No card or key; it shares Cloudflare's free daily allowance of 10,000 "neurons"
    with speech and voice. When the allowance runs out, Dewy answers that it couldn't think of an answer, and it
    resets at 00:00 UTC. Tool use on open models is less reliable than on Claude.
  - `"anthropic"`: Claude Haiku. Set a monthly spend limit at [console.anthropic.com](https://console.anthropic.com)
    first, then `npx wrangler secret put ANTHROPIC_API_KEY`. Without the key chat answers 503.
- **Tools the model can use**: weather (Open-Meteo, for the city in settings), news (BBC and The Hindu feeds), the
  notes people sent, and the study timer. The date and time come from the timezone in settings.
- **Memory**: the last 6 messages from the past 5 minutes, so "and tomorrow?" works. Transcripts are deleted after
  an hour and are never written to logs.
- **Limits per Dewy per day** (change in `wrangler.toml`): 40 questions, 60,000 model tokens, 6,000 spoken
  characters. A friendly 429 message comes back when a limit is reached.
- **Cost**: the spoken characters are most of it (Aura is about $0.015 per 1,000 characters), so a full 6,000
  characters a day is under $0.10 if you pay. On the free plan nothing is charged: requests just fail once the
  allowance is used up. Claude Haiku answers cost a fraction of a cent each. Prices change: check the Cloudflare
  and Anthropic pricing pages.
- **Testing the Cloudflare adapter locally**: set `CHAT_FAKE_AI="binding"` in `.dev.vars` and run `npm test`. The
  real Cloudflare code then talks to a pretend AI that rejects anything the real one wouldn't accept. It checks the
  code, not the quality of the real model: for that, test on the deployed address.

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
| POST | `/api/device/chat` | Dewy | Spoken question in, spoken answer (WAV) out |
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
