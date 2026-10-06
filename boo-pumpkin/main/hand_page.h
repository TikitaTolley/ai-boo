#pragma once
#include <Arduino.h>
static const char HAND_PAGE[] PROGMEM = R"BOOPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Boo Pumpkin</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#161411;color:#fff3df;font-family:system-ui,sans-serif;min-height:100vh;display:grid;place-items:center}main{width:min(94vw,960px);padding:24px 0}header{display:flex;align-items:baseline;justify-content:space-between;gap:20px}h1{font-size:32px;letter-spacing:.08em;margin:0 0 20px;color:#ff922e}#voice{font-size:24px;font-weight:700}canvas{display:block;width:100%;height:auto;aspect-ratio:4/3;background:#080808;border-radius:12px}#status{min-height:26px;font-size:16px;color:#e2d0b6}footer{font-size:13px;color:#a99d8c}
</style>
</head>
<body><main><header><h1>BOO PUMPKIN</h1><span id="voice">Voice __VOICE__</span></header>
<canvas id="view" width="320" height="240" aria-label="ESP32-CAM live view"></canvas>
<p id="status" role="status">Loading hand detection...</p><p id="detection">Detector loading...</p><footer>ESP32-CAM live view</footer>
</main>
<script type="module">
const canvas = document.querySelector('#view')
const context = canvas.getContext('2d')
const status = document.querySelector('#status')
const detection = document.querySelector('#detection')
let detector = null
let detectorError = ''
let fired = false
let pending = false
let armed = false
let clearSince = null
let hits = 0
let lastTimestamp = -1
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))

async function loadDetector() {
  try {
    const {HandLandmarker, FilesetResolver} = await import('https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.32/vision_bundle.mjs')
    const files = await FilesetResolver.forVisionTasks('https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.32/wasm')
    detector = await HandLandmarker.createFromOptions(files, {
      baseOptions: {
        modelAssetPath: 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task',
        delegate: 'CPU'
      },
      runningMode: 'VIDEO', numHands: 1,
      minHandDetectionConfidence: 0.6, minHandPresenceConfidence: 0.6,
      minTrackingConfidence: 0.6
    })
    status.textContent = 'Remove your hand briefly, then show it to the camera.'
  } catch (error) {
    detectorError = 'Hand detection could not load. Check internet access and reload the page.'
    status.textContent = detectorError
    console.error(error)
  }
}

async function triggerBoo() {
  pending = true
  // Lock this browser take even if a response is lost; never auto-retry audio.
  fired = true
  status.textContent = 'Hand detected. Sending boo...'
  try {
    const response = await fetch('/boo', {method: 'POST', signal: AbortSignal.timeout(4000)})
    if (response.status === 409) {
      status.textContent = 'This take has already triggered.'
    } else if (!response.ok) {
      throw new Error(`Playback request failed: ${response.status}`)
    } else {
      status.textContent = 'Playback command queued. Check the speaker; next voice needs an upload.'
    }
  } catch (error) {
    status.textContent = 'Playback response lost or failed. Check the speaker and Serial Monitor.'
    console.error(error)
  } finally {
    pending = false
  }
}

async function frames() {
  while (true) {
    if (document.hidden) {
      hits = 0
      clearSince = null
      armed = false
      await wait(250)
      continue
    }
    try {
      // Each detection uses a newly received ESP32 frame, not a repeated webcam frame.
      const response = await fetch('/capture', {cache: 'no-store', signal: AbortSignal.timeout(8000)})
      if (!response.ok) throw new Error(`Camera capture failed: ${response.status}`)
      const frame = await createImageBitmap(await response.blob())
      if (canvas.width !== frame.width || canvas.height !== frame.height) {
        canvas.width = frame.width
        canvas.height = frame.height
      }
      context.drawImage(frame, 0, 0)
      frame.close()
      if (detector) {
        const now = performance.now()
        const timestamp = Math.max(lastTimestamp + 1, now)
        lastTimestamp = timestamp
        const result = detector.detectForVideo(canvas, timestamp)
        const found = result.landmarks.length > 0
        detection.textContent = found ? 'Hand detected - blue dots mark landmarks.' : 'Detector running - no hand detected.'
        context.fillStyle = '#24baff'
        for (const hand of result.landmarks) {
          for (const point of hand) {
            context.beginPath()
            context.arc(point.x * canvas.width, point.y * canvas.height, 2, 0, Math.PI * 2)
            context.fill()
          }
        }
        if (!fired && !pending) {
        if (!found) {
          hits = 0
          clearSince ??= now
          if (now - clearSince >= 750) armed = true
          status.textContent = armed ? 'Ready. Show your hand.' : 'Remove your hand briefly.'
        } else {
          clearSince = null
          hits++
          status.textContent = armed ? `Hand detected: ${hits}/3 frames confirmed.` : 'Hand detected. Move it out briefly to arm playback.'
          if (armed && hits >= 3) await triggerBoo()
        }
        }
      } else if (detectorError) {
        detection.textContent = 'Detector unavailable.'
        status.textContent = detectorError
      }
    } catch (error) {
      hits = 0
      clearSince = null
      armed = false
      detection.textContent = 'No current detection result.'
      status.textContent = 'Camera frame or detection failed. Check browser console and Serial Monitor.'
      console.error(error)
      await wait(1000)
    }
    await wait(60)
  }
}
loadDetector()
frames()
</script>
</body></html>
)BOOPAGE";
