#include <Arduino.h>
#include <WiFi.h>
#include <stdarg.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "mbedtls/base64.h"
#include "audio_control.h"
#include "camera_lock.h"
#include "detector.h"
#include "web_server.h"
#include "secrets.h"

#ifndef PAGE_PASSWORD
#error "Add PAGE_PASSWORD to secrets.h (see secrets.example.h)"
#endif

constexpr int LOG_LINES = 15;
static String logLines[LOG_LINES];
static int nextLogLine = 0;
// A mutex, not a critical section: copying Strings allocates memory, which must not happen with interrupts off.
static SemaphoreHandle_t logMutex = xSemaphoreCreateMutex();

void logEvent(const char *format, ...) {
  char text[120];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  Serial.println(text);
  const String line = String(millis() / 1000) + "s  " + text;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  logLines[nextLogLine] = line;
  nextLogLine = (nextLogLine + 1) % LOG_LINES;
  xSemaphoreGive(logMutex);
}

static esp_err_t sendJpeg(httpd_req_t *req, uint8_t *jpg, size_t jpgLen) {
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const esp_err_t result = httpd_resp_send(req, reinterpret_cast<const char *>(jpg), jpgLen);
  free(jpg);
  return result;
}

static esp_err_t captureHandler(httpd_req_t *req) {
  camera_fb_t *fb = grabFrame();
  if (!fb) {
    logEvent("[CAMERA] Capture failed");
    return httpd_resp_send_500(req);
  }
  uint8_t *jpg = nullptr;
  size_t jpgLen = 0;
  const bool converted = frame2jpg(fb, 80, &jpg, &jpgLen);
  releaseFrame(fb);
  if (!converted) return httpd_resp_send_500(req);
  return sendJpeg(req, jpg, jpgLen);
}

static esp_err_t inputHandler(httpd_req_t *req) {
  static uint8_t gray[DETECTOR_SIZE * DETECTOR_SIZE];
  copyDetectorInput(gray);
  uint8_t *jpg = nullptr;
  size_t jpgLen = 0;
  if (!fmt2jpg(gray, sizeof(gray), DETECTOR_SIZE, DETECTOR_SIZE, PIXFORMAT_GRAYSCALE, 90, &jpg, &jpgLen)) {
    return httpd_resp_send_500(req);
  }
  return sendJpeg(req, jpg, jpgLen);
}

static esp_err_t booHandler(httpd_req_t *req) {
  // A deliberate speaker test, so it plays even in quiet mode.
  const bool queued = queueBoo(configuredBooTrack(), true);
  if (queued) logEvent("[BOO] Played from /boo");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, queued
    ? "<meta http-equiv=refresh content=\"1;url=/status\">Boo!"
    : "<meta http-equiv=refresh content=\"2;url=/status\">Cooling down after the last boo");
}

static int queryInt(httpd_req_t *req, const char *key, int fallback) {
  char query[48];
  char value[12];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return fallback;
  if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) return fallback;
  return atoi(value);
}

static esp_err_t backToStatus(httpd_req_t *req) {
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/status");
  return httpd_resp_send(req, nullptr, 0);
}

// /volume?level=0-30
static esp_err_t volumeHandler(httpd_req_t *req) {
  setVolume(queryInt(req, "level", currentVolume()));
  logEvent("[AUDIO] Volume %d/30", currentVolume());
  return backToStatus(req);
}

// /quiet?on=1 silences detection boos, /quiet?on=0 brings the sound back
static esp_err_t quietHandler(httpd_req_t *req) {
  setQuiet(queryInt(req, "on", 1) != 0);
  logEvent("[AUDIO] Quiet mode %s", isQuiet() ? "on" : "off");
  return backToStatus(req);
}

// One line of key=value pairs for scripts such as capture.sh
static esp_err_t stateHandler(httpd_req_t *req) {
  const DetectorStats detector = detectorStats();
  char line[96];
  snprintf(line, sizeof(line), "score=%d boos=%d quiet=%d volume=%d runs=%lu\n", detector.score, booCount(),
    isQuiet() ? 1 : 0, currentVolume(), static_cast<unsigned long>(detector.runs));
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, line);
}

// Text under the pictures: live numbers, then the recent events.
static esp_err_t infoHandler(httpd_req_t *req) {
  const DetectorStats detector = detectorStats();
  const long sinceBoo = booCount() ? static_cast<long>(msSinceLastBoo() / 1000) : -1;
  String text;
  text.reserve(2000);
  text += "Person score  " + (detector.score < 0 ? String("-") : String(detector.score) + "%");
  text += "\nInference     " + String(detector.inferenceMs) + " ms (" + String(detector.runs) + " runs)";
  text += "\nBoos          " + String(booCount()) + (sinceBoo >= 0 ? ", last " + String(sinceBoo) + "s ago" : "");
  text += "\nSound         volume " + String(currentVolume()) + "/30" + (isQuiet() ? ", QUIET (boos are silent)" : "");
  text += "\nNetwork       " + WiFi.SSID() + " " + WiFi.localIP().toString() + " (" + String(WiFi.RSSI()) + " dBm)";
  text += "\nUptime        " + String(millis() / 1000) + "s, free memory " + String(ESP.getFreeHeap() / 1024) + " KB\n\n";
  xSemaphoreTake(logMutex, portMAX_DELAY);
  for (int i = 0; i < LOG_LINES; i++) {
    const String &line = logLines[(nextLogLine + i) % LOG_LINES];
    if (line.length()) text += line + "\n";
  }
  xSemaphoreGive(logMutex);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, text.c_str(), text.length());
}

// The page loads once; the script swaps in each new picture only after it has fully arrived, so nothing
// jumps or blanks. Pictures sit in fixed-size boxes above the text.
static const char STATUS_PAGE[] = R"PAGE(<!doctype html>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Boo Pumpkin</title>
<style>
body{font:14px monospace;margin:12px;background:#161411;color:#fff3df}
h2{margin:0 0 10px;color:#ff922e}
.views{display:grid;grid-template-columns:3fr 4fr;gap:8px;max-width:640px}
.views img{display:block;width:100%;background:#000;border-radius:6px;object-fit:cover}
#input{aspect-ratio:1;image-rendering:pixelated}#frame{aspect-ratio:4/3}
.views span{font-size:12px;color:#a99d8c}
.controls{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}
button{font:inherit;background:#2a241d;color:#ff922e;border:1px solid #ff922e;border-radius:6px;padding:8px 12px}
pre{white-space:pre-wrap;margin:0}
</style>
<h2>Boo Pumpkin</h2>
<div class=views><div><img id=input alt="What the model sees"><span>model sees</span></div>
<div><img id=frame alt="Camera"><span>camera</span></div></div>
<div class=controls><button data-go="/boo">Play boo</button><button data-vol=-2>Vol -</button>
<button data-vol=2>Vol +</button><button data-go="/quiet?on=1">Quiet</button><button data-go="/quiet?on=0">Sound on</button></div>
<pre id=info>Loading...</pre>
<script>
const info = document.querySelector('#info')
let volume = 0
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
async function swapImage(img, url) {
  const response = await fetch(url, {cache: 'no-store'})
  if (!response.ok) return
  const next = URL.createObjectURL(await response.blob())
  const old = img.src
  img.src = next
  if (old.startsWith('blob:')) URL.revokeObjectURL(old)
}
async function refreshInfo() {
  const response = await fetch('/info', {cache: 'no-store'})
  if (!response.ok) return
  info.textContent = await response.text()
  volume = Number((info.textContent.match(/volume (\d+)/) || [0, 0])[1])
}
async function loop() {
  while (true) {
    try {
      await swapImage(document.querySelector('#input'), '/input')
      await swapImage(document.querySelector('#frame'), '/capture')
      await refreshInfo()
    } catch (error) {
      info.textContent = 'Cannot reach the pumpkin, retrying...'
    }
    await wait(700)
  }
}
document.querySelectorAll('button').forEach(button => button.addEventListener('click', async () => {
  const url = button.dataset.go ?? `/volume?level=${Math.max(0, Math.min(30, volume + Number(button.dataset.vol)))}`
  await fetch(url, {cache: 'no-store'}).catch(() => {})
  refreshInfo().catch(() => {})
}))
loop()
</script>
)PAGE";

static esp_err_t statusHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, STATUS_PAGE, sizeof(STATUS_PAGE) - 1);
}

// Every page asks for user "boo" and PAGE_PASSWORD, so other people on the same Wi-Fi cannot watch or trigger it.
static String expectedAuth;

static esp_err_t requirePassword(httpd_req_t *req) {
  char header[96] = "";
  const size_t length = httpd_req_get_hdr_value_len(req, "Authorization");
  const bool authorised = length > 0 && length < sizeof(header) &&
    httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK && expectedAuth == header;
  if (!authorised) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Boo Pumpkin\"");
    return httpd_resp_sendstr(req, "Password needed");
  }
  const auto handler = reinterpret_cast<esp_err_t (*)(httpd_req_t *)>(req->user_ctx);
  return handler(req);
}

bool startWebServer() {
  const String credentials = String("boo:") + PAGE_PASSWORD;
  unsigned char encoded[96];
  size_t encodedLen = 0;
  if (mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen,
        reinterpret_cast<const unsigned char *>(credentials.c_str()), credentials.length()) != 0) {
    return false;
  }
  expectedAuth = "Basic " + String(reinterpret_cast<const char *>(encoded), encodedLen);

  // Byte order for the RGB565 -> JPEG conversion. If colours look wrong, try false.
  jpgSetRgb565BE(true);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 12;  // The default of 8 is already full
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) return false;

  struct Route {
    const char *uri;
    esp_err_t (*handler)(httpd_req_t *);
  };
  const Route routes[] = {
    {"/", statusHandler},
    {"/status", statusHandler},
    {"/capture", captureHandler},
    {"/input", inputHandler},
    {"/boo", booHandler},
    {"/volume", volumeHandler},
    {"/quiet", quietHandler},
    {"/state", stateHandler},
    {"/info", infoHandler},
  };
  bool registered = true;
  for (const Route &route : routes) {
    httpd_uri_t uri = {};
    uri.uri = route.uri;
    uri.method = HTTP_GET;
    uri.handler = requirePassword;
    uri.user_ctx = reinterpret_cast<void *>(route.handler);
    registered = httpd_register_uri_handler(server, &uri) == ESP_OK && registered;
  }
  return registered;
}
