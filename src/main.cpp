#include <WiFi.h>
#include "SinricPro.h"
#include "SinricProSwitch.h"
#include <map>

// --------------- Configuration Flags ----------------
// WiFi will always attempt to connect, but switches work independently

// --------------- WiFi + SinricPro Credentials ----------------
// Credentials and device IDs are injected at build time via build_flags
// to keep secrets out of source control. The build system (PlatformIO)
// will use environment variables (from `.env`) to set these with
// e.g. -D WIFI_SSID="yourssid"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#ifndef APP_KEY
#define APP_KEY ""
#endif

#ifndef APP_SECRET
#define APP_SECRET ""
#endif

// --------------- Device IDs ----------------------------------
#ifndef DEVICE_ID_1
#define DEVICE_ID_1 ""
#endif

#ifndef DEVICE_ID_2
#define DEVICE_ID_2 ""
#endif

#ifndef DEVICE_ID_3
#define DEVICE_ID_3 ""
#endif


// --------------- GPIO Mapping -------------------------------
#define RelayPin1  5    // GPIO5
#define RelayPin2  18   // GPIO18
#define RelayPin3  19   // GPIO19


#define SwitchPin1  32  // GPIO32
#define SwitchPin2  33  // GPIO33
#define SwitchPin3  25  // GPIO25


#define wifiLed 2       // Built-in LED on ESP32

#define BAUD_RATE 9600
#define DEBOUNCE_TIME 250

// Relay polarity mapping (active-low relay boards)
#define RELAY_ON_LEVEL  LOW   // device ON when relay input is LOW
#define RELAY_OFF_LEVEL HIGH  // device OFF when relay input is HIGH

// Additional switch handling constants
#define MIN_TOGGLE_INTERVAL_MS 600
#define SWITCH_SAMPLE_COUNT 5
#define SWITCH_SAMPLE_DELAY_MS 1

// --------------- Structures for Configuration ----------------
typedef struct {
  int relayPIN;
  int switchPIN;
} deviceConfig_t;

typedef struct {
  String deviceId;
  bool lastSwitchState;
  unsigned long lastSwitchChange;
  unsigned long lastActionTime;  // last time we toggled due to this switch
  bool waitingForRelease;        // true after press until release detected
} switchConfig_t;

// Map each deviceId to its relay/switch pair
std::map<String, deviceConfig_t> devices = {
  {DEVICE_ID_1, {RelayPin1, SwitchPin1}},
  {DEVICE_ID_2, {RelayPin2, SwitchPin2}},
  {DEVICE_ID_3, {RelayPin3, SwitchPin3}}
};


// For debounce handling
std::map<int, switchConfig_t> switches;

// Track current power state for each device (avoids reading OUTPUT pins)
std::map<String, bool> devicePowerState; // true = device ON, false = device OFF
// Track states that need to be synced to cloud on next connection
std::map<String, bool> dirtyStateToSync;

// WiFi / SinricPro reconnection helpers
unsigned long lastWifiAttemptMs = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
bool sinricInitialized = false;
bool devicesRegistered = false;
wl_status_t lastWifiStatus = WL_DISCONNECTED;

// ---------------- SETUP FUNCTIONS ------------------

void setupRelays() {
  for (auto &device : devices) {
    int relayPIN = device.second.relayPIN;
    pinMode(relayPIN, OUTPUT);
    // Boot defaults per wiring: Device1 ON => Relay OFF; others OFF => Relay ON
    bool initialPowerOn = (device.first == DEVICE_ID_1);
    devicePowerState[device.first] = initialPowerOn;
    digitalWrite(relayPIN, initialPowerOn ? RELAY_OFF_LEVEL : RELAY_ON_LEVEL);
    Serial.printf("Relay %d initialized - %s\n", relayPIN, initialPowerOn ? "ON" : "OFF");
    // Mark for initial cloud sync after connection
    dirtyStateToSync[device.first] = true;
  }
}

void setupSwitches() {
  for (auto &device : devices) {
    switchConfig_t swConfig;
    swConfig.deviceId = device.first;
    swConfig.lastSwitchChange = 0;
    swConfig.lastSwitchState = HIGH; // idle state (not pressed)
    swConfig.lastActionTime = 0;
    swConfig.waitingForRelease = false;

    int pin = device.second.switchPIN;
    pinMode(pin, INPUT_PULLUP); // internal pull-up, active LOW
    switches[pin] = swConfig;
  }
}

// ---------------- SINRIC EVENT HANDLER ------------------

bool onPowerState(String deviceId, bool &state) {
  Serial.printf("%s => %s\n", deviceId.c_str(), state ? "ON" : "OFF");
  int relayPIN = devices[deviceId].relayPIN;

  // Wiring mapping: Relay ON cuts power; Device ON => Relay OFF
  digitalWrite(relayPIN, state ? RELAY_OFF_LEVEL : RELAY_ON_LEVEL);
  devicePowerState[deviceId] = state;
  // No need to send event back for commands we received

  return true;
}

// ---------------- HANDLE PHYSICAL SWITCHES ------------------

static bool readStableSwitch(int pin) {
  int highCount = 0;
  for (int i = 0; i < SWITCH_SAMPLE_COUNT; i++) {
    highCount += (digitalRead(pin) == HIGH);
    delay(SWITCH_SAMPLE_DELAY_MS);
  }
  return highCount >= (SWITCH_SAMPLE_COUNT / 2 + 1);
}

void handleSwitches() {
  unsigned long now = millis();

  for (auto &sw : switches) {
    int pin = sw.first;
    bool currentState = readStableSwitch(pin);
    bool lastState = sw.second.lastSwitchState;

    if (currentState != lastState && now - sw.second.lastSwitchChange > DEBOUNCE_TIME) {
      sw.second.lastSwitchChange = now;
      sw.second.lastSwitchState = currentState;

      // Edge-based press detection with release gating and min interval
      if (!sw.second.waitingForRelease && lastState == HIGH && currentState == LOW) {
        if (now - sw.second.lastActionTime >= MIN_TOGGLE_INTERVAL_MS) {
          String deviceId = sw.second.deviceId;
          int relayPIN = devices[deviceId].relayPIN;
          bool newPowerState = !devicePowerState[deviceId];
          devicePowerState[deviceId] = newPowerState;
          // Apply wiring mapping: Device ON => Relay OFF
          digitalWrite(relayPIN, newPowerState ? RELAY_OFF_LEVEL : RELAY_ON_LEVEL);
          sw.second.lastActionTime = now;
          sw.second.waitingForRelease = true;

          Serial.printf("Switch %d pressed - Relay %d: %s\n", pin, relayPIN, newPowerState ? "ON" : "OFF");

          // Mark for cloud sync; will be sent immediately if connected
          dirtyStateToSync[deviceId] = true;
        }
      }
    }

    // Detect stable release to re-arm
    if (sw.second.waitingForRelease && currentState == HIGH && (now - sw.second.lastSwitchChange) > 30) {
      sw.second.waitingForRelease = false;
    }
  }
}

// ---------------- WIFI & SINRIC ------------------

void setupWiFi() {
  Serial.println("Attempting WiFi connection...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    digitalWrite(wifiLed, LOW);
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed - Physical switches will still work!");
    digitalWrite(wifiLed, HIGH);
  }
}

void registerSinricDevices() {
  if (devicesRegistered) return;
  for (auto &device : devices) {
    const char *deviceId = device.first.c_str();
    SinricProSwitch &mySwitch = SinricPro[deviceId];
    mySwitch.onPowerState(onPowerState);
  }
  devicesRegistered = true;
  Serial.println("SinricPro devices registered");
}

void ensureWifiAndSinric() {
  wl_status_t status = WiFi.status();

  // Update LED on state changes
  if (status != lastWifiStatus) {
    if (status == WL_CONNECTED) {
      digitalWrite(wifiLed, LOW);
      Serial.println("WiFi connected");
      Serial.println(WiFi.localIP());
    } else {
      digitalWrite(wifiLed, HIGH);
      Serial.println("WiFi disconnected");
    }
    lastWifiStatus = status;
  }

  // Attempt periodic WiFi reconnect if not connected
  if (status != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiAttemptMs = now;
      Serial.println("Re-attempting WiFi connection...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    // If WiFi is down, mark Sinric uninitialized so we re-begin next time
    sinricInitialized = false;
    return;
  }

  // WiFi is connected: ensure devices registered and SinricPro running
  registerSinricDevices();
  if (!sinricInitialized) {
    SinricPro.begin(APP_KEY, APP_SECRET);
    // Do not restore cloud states; keep local boot defaults authoritative
    SinricPro.restoreDeviceStates(false);
    sinricInitialized = true;
    Serial.println("SinricPro initialized / resumed");
  }
  
  // If SinricPro transport is available, push any pending state updates
  if (SinricPro.isConnected()) {
    for (auto &entry : dirtyStateToSync) {
      if (entry.second) {
        const String &devId = entry.first;
        bool state = devicePowerState[devId];
        SinricProSwitch &mySwitch = SinricPro[devId.c_str()];
        mySwitch.sendPowerStateEvent(state);
        entry.second = false;
      }
    }
  }
}

// ---------------- MAIN ------------------

void setup() {
  Serial.begin(BAUD_RATE);
  delay(1000);
  Serial.println("\n=== Home Automation System Starting ===");
  
  pinMode(wifiLed, OUTPUT);
  digitalWrite(wifiLed, HIGH);

  setupRelays();
  setupSwitches();
  setupWiFi();
  registerSinricDevices();
  // After initial attempt, ongoing management happens in loop()
  
  Serial.println("System ready! Press switches to control relays.");
}

void loop() {
  // Maintain WiFi and SinricPro connections
  ensureWifiAndSinric();
  // Always call handle; library ignores if not fully connected
  SinricPro.handle();
  
  // Always handle physical switches regardless of WiFi status
  handleSwitches();
}
