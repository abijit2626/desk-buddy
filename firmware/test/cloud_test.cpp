// Runs Dewy's real cloud code (sync, pairing card, notes, timer, settings, firmware check, setup page)
// against a fake server, on a PC.
#include "Arduino.h"
#include "Wire.h"
SerialT Serial; TwoWire Wire; int g_wifiStatus = 3;
#include "wake_stub.h"
#include "../dewy/dewy.ino"
namespace lgfx { inline namespace v1 { namespace fonts { const GFXfont FreeSansBold12pt7b{}, FreeSansBold18pt7b{}, FreeSansBold24pt7b{}; } } }
#include <vector>

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", name); (ok ? passed : failed)++; }

// ---- a fake Dewy server
static std::string lastUrl, lastHeaders;
static int syncCode = 200;
static bool offerFirmware = false;
static std::string fwBytes(200000, '\0'), fwSha;
static std::string sha256hex(const std::string& data) {
  mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, (const uint8_t*)data.data(), data.size());
  uint8_t d[32]; mbedtls_sha256_finish(&c, d); char h[65]; for (int i = 0; i < 32; i++) snprintf(h + 2 * i, 3, "%02x", d[i]); return h;
}
static MockReply server(const std::string& m, const std::string& url, const std::string&, const std::string& headers) {
  lastUrl = url; lastHeaders = headers;
  if (url == "https://dewy.test/api/device/register" && m == "POST") {
    if (headers.find("X-Register-Key: test-register-key") == std::string::npos) return { 403, "{\"error\":\"wrong register key\"}" };
    return { 201, "{\"deviceId\":\"d1\",\"token\":\"tok_abcdefghijklmnopqrstuvwxyz0123456789\"}" };
  }
  if (url.rfind("https://dewy.test/api/device/sync", 0) == 0) {
    if (syncCode != 200) return { syncCode, "{}" };
    std::string fw = offerFirmware ? ",\"firmware\":{\"version\":\"1.1.0\",\"url\":\"https://github.com/x/dewy.bin\",\"sha256\":\"" + fwSha + "\",\"size\":" + std::to_string(fwBytes.size()) + "}" : "";
    return { 200, "{\"now\":1,\"rev\":3,\"paired\":false,\"pairCode\":\"ABCD2345\",\"events\":["
                  "{\"id\":7,\"kind\":\"timer_start\",\"minutes\":30},"
                  "{\"id\":8,\"kind\":\"note\",\"text\":\"Good luck today!\",\"from\":\"Abijit\"}],"
                  "\"settings\":{\"name\":\"Dewy\",\"timezone\":\"<+07>-7\",\"city\":{\"name\":\"Bangkok\",\"lat\":13.75,\"lon\":100.5},"
                  "\"alarms\":[{\"id\":\"a1\",\"time\":\"06:45\",\"days\":[1,2,3,4,5],\"label\":\"College\",\"on\":true}],"
                  "\"reminders\":{\"water\":60,\"stretch\":0,\"from\":8,\"to\":22},\"studyMinutes\":45,"
                  "\"birthday\":{\"month\":3,\"day\":14},\"giftDate\":{\"year\":2026,\"month\":12,\"day\":20}}" + fw + "}" };
  }
  if (url == "https://github.com/x/dewy.bin") return { 200, fwBytes };
  return { 404, "{}" };
}

int main() {
  g_http = server;
  for (size_t i = 0; i < fwBytes.size(); i++) fwBytes[i] = char((i * 31 + 7) & 255);
  fwSha = sha256hex(fwBytes);
  setup();
  netPrefs.begin("buddy", false);

  printf("-- register and sync\n");
  check("registers with the register key", cloudRegister() && strlen(deviceToken) > 20);
  cloudSync();
  check("sync sends the device token and version", lastHeaders.find("Authorization: Bearer tok_") != std::string::npos && lastHeaders.find("X-Dewy-Version: 1.0.0") != std::string::npos);
  for (int i = 0; i < 3; i++) { pollCloud(millis() + 1000 * (i + 1)); loop(); }
  check("alarm 06:45 on weekdays arrives", S.nAlarms == 1 && S.alarms[0].h == 6 && S.alarms[0].m == 45 && S.alarms[0].dayMask == 0b0111110);
  check("reminders, study length, birthday, gift date arrive", S.water == 60 && S.stretch == 0 && S.from == 8 && S.to == 22 && S.study == 45 && S.bdM == 3 && S.bdD == 14 && S.gY == 2026 && S.gD == 20);
  check("time zone and city for the weather", !strcmp(S.tz, "<+07>-7") && S.hasCity && cityLat > 13.7f && cityLon > 100.4f);
  check("settings are saved for the next start", prefs.getString("settings", "").length() > 50);
  check("the study timer starts", buddy.timerActive() && buddy.timerLeftMs() > 29 * 60000);
  check("events are acknowledged", ackId == 8);
  cloudSync();
  check("the next sync tells the server", lastUrl.find("ack=8") != std::string::npos && lastUrl.find("rev=3") != std::string::npos);
  check("the same events aren't handled twice", inbox.nev == 0);
  check("the pairing card shows the code", buddy.bannerShown() && banner == BANNER_PAIR);

  printf("-- note queue\n");
  check("while the pairing card is up, the note waits", noteQn == 1);
  bannerHiddenUntil = millis() + 60000; setBanner(BANNER_NONE);          // she taps the card away
  for (int i = 0; i < 3; i++) { pollCloud(millis() + 5000 + 1000 * i); loop(); }
  check("then the note shows, with Abijit's name", noteQn == 0 && buddy.overlayActive() && !buddy.bannerShown());

  printf("-- firmware update\n");
  offerFirmware = true; cloudSync();
  { NetLock l; check("firmware 1.1.0 is offered", inbox.fwOffer && !strcmp(inbox.fwVersion, "1.1.0")); }
  installFirmware();
  check("downloaded, SHA-256 checked, installed", Update.ended && fwProgress == 1);
  fwProgress = -1; Update.ended = false;
  fwSha[0] = fwSha[0] == 'a' ? 'b' : 'a'; offerFirmware = true; cloudSync();
  installFirmware();
  check("a file with the wrong SHA-256 is not installed", !Update.ended && fwProgress < 0);
  offerFirmware = false;

  printf("-- server forgets the device\n");
  syncCode = 401; cloudSync(); syncCode = 200;
  check("token cleared so it registers again", deviceToken[0] == 0 && netPrefs.getString("token", "").length() == 0);

  printf("-- setup page\n");
  portalRoot();
  check("network names are escaped on the page", portal.lastBody.find("Home &lt;Wi-Fi&gt;") != std::string::npos && portal.lastBody.find("Dewy&#39;s &quot;home&quot;") != std::string::npos && portal.lastBody.find("<Wi-Fi>") == std::string::npos);
  portal.args = { { "s", "Home" }, { "o", "" }, { "p", "short" } };
  portalSave();
  check("a 5-character password is refused", portal.lastCode == 303 && strlen(portalMsg) > 0);
  portal.args = { { "s", "Home" }, { "o", "Flat 4B" }, { "p", "correct horse" } };
  portalSave();
  check("a typed network name wins and is saved", prefs.getString("wifi_ssid", "") == "Flat 4B" && prefs.getString("wifi_pass", "") == "correct horse");

  printf("\n%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
