# Boo Pumpkin
A Halloween pumpkin that plays a spooky boo through a speaker when triggered by its camera.

## Recording a take

Open `main/main.ino` in Arduino IDE. Set `BOO_VOICE` near the top to 1 (ghost), 2 (demon), or 3 (witch), then upload to AI Thinker ESP32-CAM. The board plays at most once per boot, even if the browser reloads. There is no reset button or voice selector in the page.

Open the HTTP address printed in Serial Monitor at 115200 baud. The page always shows the ESP32-CAM. Keep your hand out briefly until it says Ready, then show your hand to trigger the DFPlayer. Change `BOO_VOICE` and upload again for the next take. Serial Monitor command B manually triggers the selected voice and consumes that take.

The webpage is embedded in the Arduino firmware. MediaPipe runs inside the laptop browser on frames received from the ESP32; it does not use the laptop webcam. No separate Python script or local server is needed. Internet access is needed to download MediaPipe and its model on page load. Frames are processed in the browser and are not uploaded to those model hosts.

## Wi-Fi

Put your 2.4 GHz network details in `main/secrets.h`, using `secrets.example.h` as a template. The credentials file is git-ignored.

## Wiring (disconnect power first)

| From | To |
| --- | --- |
| ESP32 GPIO14 | 1 kohm resistor, then DFPlayer RX |
| ESP32 GND | DFPlayer GND and power supply GND |
| Regulated 5V | DFPlayer VCC |
| Speaker positive | DFPlayer SPK1 |
| Speaker negative | DFPlayer SPK2 |

Neither speaker terminal goes to GND. DFPlayer TX may be left disconnected. The DFPlayer holds the audio microSD; leave the ESP32 SD slot empty because GPIO13/14 are reserved for audio UART. The CAM-MB USB connection powers the ESP32 during initial tests. Do not tie two independent 5V sources together. Initial audio volume is 8/30.

## Sources

Camera server adapted from Espressif Arduino-ESP32 3.3.11 CameraWebServer. Browser hand detection uses MediaPipe Tasks Vision 0.10.32 and Google's Hand Landmarker model.

- https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker/web_js
- https://wiki.dfrobot.com/dfr0299/docs/20905

## Build video

[Choosing a Boo for My Halloween Build](https://www.youtube.com/watch?v=T6uCIbbO7LQ)
