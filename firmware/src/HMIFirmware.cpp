// =================================================================
// --- LIBRARIES ---
// =================================================================
#include <WiFi.h>
#include <ESP32RotaryEncoder.h>
#include "NextionX2.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

// =================================================================
// --- CONFIGURATION & DEFINES ---
// =================================================================
const bool SIMULATION_MODE = false;
const bool OFFLINE_MODE = false;
const float TEMP_SETPOINT_SCALE = 10.0f;
const int SHOT_RETENTION_TIME_MS = 10000;
const int CHART_RETENTION_TIME_MS = 3000;

// =================================================================
// --- HARDWARE PIN DEFINITIONS ---
// =================================================================
const uint8_t BUTTON_PIN = D5;
const uint8_t ENCODER_PIN_A = D4;
const uint8_t ENCODER_PIN_B = D3;
const uint8_t SERIAL_RX = D9;
const uint8_t SERIAL_TX = D8;

// =================================================================
// --- NETWORK CONFIGURATION ---
// =================================================================
// --- Wi-Fi Credentials ---
const char *WIFI_SSID = "";
const char *WIFI_PASSWORD = "";

// --- OTA (Over-the-Air Updates) ---
const char *OTA_HOSTNAME = "esp32-arduino-screen";
const char *OTA_PASSWORD = "1234";

// Received bools for settings
bool brewModeReceived = false;
bool steamBoostReceived = false; // Flag to know if false is real or default
bool flatValueReceived = false;
bool profilingSourceReceived = false;
bool profilingTargetReceived = false;
bool profilingModeReceived = false;
bool currentProfileReceived = false;
bool tempsetReceived = false;

// --- Temporary Buffers for Web Forms ---
char form_mqtt_server[40] = ""; // Default values for the form
char form_mqtt_port_str[6] = "1883";
char form_mqtt_user[33] = "";
char form_mqtt_password[33] = "";

// --- WebServer for Connected Config ---
WebServer server(80);
bool portalRunning = false;
bool mqttSettingsPending = false;

// --- MQTT Topics ---
const char *mqtt_topic_state = "state";
const char *mqtt_topic_boiler_temp = "boiler_temp";
const char *mqtt_topic_hx_temp = "hx_temp";
const char *mqtt_topic_pressure = "pressure";
const char *mqtt_topic_heater = "heater";
const char *mqtt_topic_pump = "pump";
const char *mqtt_topic_brew_mode = "brew_mode";
const char *mqtt_topic_lever = "lever";
const char *mqtt_topic_setpoint = "tempsetbrew";
const char *mqtt_topic_weight = "weight";
const char *mqtt_topic_flow_rate = "flow_rate";
const char *mqtt_topic_steam_boost_status = "steam_boost";
const char *mqtt_topic_set_profiling_mode = "profiling_mode";
const char *mqtt_topic_set_profiling_source = "profiling_source";
const char *mqtt_topic_set_profiling_target = "profiling_target";
const char *mqtt_topic_set_profiling_flat = "profiling_flat_value";
const char *mqtt_topic_mqtt_server = "mqtt_server";
const char *mqtt_topic_mqtt_port = "mqtt_port";
const char *mqtt_topic_mqtt_user = "mqtt_user";
const char *mqtt_topic_mqtt_pass = "mqtt_pass";
const char *mqtt_topic_import_profile = "profile_data";

// =================================================================
// --- SYSTEM & LIBRARY OBJECTS ---
// =================================================================
// --- Hardware ---
RotaryEncoder rotaryEncoder(ENCODER_PIN_A, ENCODER_PIN_B, BUTTON_PIN);
const long ROTARY_MIN_BOUND = 0;
const long ROTARY_MAX_BOUND = 12;

// --- Nextion Display Communication Port ---
NextionComPort nextion;

// --- ESP-NOW Auto-Pairing ---
#define PAIR_REQUEST 1
#define PAIR_RESPONSE 2
char espNowMessageBuffer[250] = "";
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t mainControllerMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
int myChannel = 0;
esp_now_peer_info_t peerInfo;

// Define a structure for ESP-NOW messages
typedef struct struct_message
{
  char payload[250];
} struct_message;
typedef struct struct_pairing
{
  uint8_t id;
  uint8_t macAddr[6];
  uint8_t channel;
  char identifier[10];
} struct_pairing;
const char *espIdentifier = "espresso";

struct_message myData;
struct_pairing pairingData;
// =================================================================
// --- GLOBAL STATE & CONTROL VARIABLES ---
// =================================================================
// --- Machine State (from MQTT) ---
float boilerTemp = 0.0f;
float brewTempSetPoint = 0.0f;
float hxTemp = 0.0f;
float pressure = 0.0f;
float weight = 0.0f;
bool isHeaterOn = false;
bool brewLeverLifted = false;
volatile bool pumpIsOn = false;
char machineState[64] = "";
char profilingMode[20] = "";
char profilingSource[20] = "pressure";
char profilingTarget[20] = "time";
unsigned long calibrationRequestTime = 0;
const long CALIBRATION_REQUEST_TIMEOUT_MS = 10000; // 10-second timeout
int calibrationStep = 0;                           // 0=Off, 1=Waiting for Tare, 2=Waiting for Weigh
unsigned long systemMessageClearTime = 0;
bool cleaningCycleActive = false;
unsigned long cleaningRequestTime = 0;
const long CLEANING_REQUEST_TIMEOUT_MS = 10000;
int cleaningCycleCount = 0;
float referenceWeight;
unsigned long chartStopTime = 0;

// --- ESP Now paring variables ---
unsigned long lastPairingRequestTime = 0;
const long PAIRING_REQUEST_INTERVAL_MS = 10000;
bool isPaired = false;
unsigned long lastControllerMessageTime = 0;
const long CONTROLLER_TIMEOUT_MS = 10000;

// --- Display & UI State ---
volatile int currentPage = 0;
int lastPageForSelection = 0;
volatile int currentSelectionIndex = 0;
volatile bool isItemSelected = false;

// --- Input Handling (Encoder & Button) ---
volatile long lastEncoderValue = 0;
volatile long encoderTicks = 0;
volatile bool buttonPressed = false;
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long lastEncoderActivityTime = 0;
int pendingSettingIndex = -1;
const int SETTING_ID_FLAT_VALUE = 100;
const int SETTING_ID_PROFILING_VALUE = 101;
const long PUBLISH_TIMEOUT = 1000;

// --- Shot & Flow Tracking ---
int shotTime = 0;
unsigned long shotStartTimeMillis = 0;
const int SHOT_START_THRESHOLD_S = 1;
bool shotIsActive = false;
const int MAX_SHOT_TIME_S = 50;

// --- Flow Rate Calculation ---
float flowRate = 0.0f;

// --- Raw Debug Data (for plotdebug mode) ---
float rawWeight = 0.0f;
float filteredWeight = 0.0f;
float filteredFlow = 0.0f;
unsigned long lastDebugDataTime = 0;
const long DEBUG_PLOT_TIMEOUT_MS = 2000;

// --- New scales for the debug plot mode ---
const float DEBUG_WEIGHT_MAX = 200.0;
const float DEBUG_WEIGHT_MIN = 0.0;
const float DEBUG_FLOW_MAX = 10.0;
const float DEBUG_FLOW_MIN = 0.0;

// --- Value Caching for Display Optimization ---
int lastShotTime_sent = -1;
float lastHxTemp_sent = -1.0f;
float lastBoilerTemp_sent = -1.0f;
float lastPressure_sent = -1.0f;
float lastTempSetpoint_sent = 0.0f;
float lastWeight_sent = -1.0f;
char lastMachineState_sent[64] = "";

// --- Settings Request Timer ---
unsigned long lastSettingsRequestTime = 0;
const long SETTINGS_REQUEST_INTERVAL_MS = 10000;

struct ProfileStep
{
  float target;
  float control;
};

struct EspressoProfile
{
  char name[65];
  bool isStepped;
  int numSteps;
  ProfileStep steps[128];
};

EspressoProfile *currentProfile = nullptr;
#define MAX_PROFILES 32
EspressoProfile profiles[MAX_PROFILES];
int currentProfileIndex = 0;
Preferences preferences;

bool currentProfileDirty = false;
unsigned long profileIndexSettleTime = 0;
bool profileIndexPendingSave = false;
const unsigned long PROFILE_SAVE_DELAY_MS = 3000;

// Webserver
bool profileWebserverRunning = false;

// =================================================================
// --- NEXTION COMPONENT DEFINITIONS ---
// =================================================================
struct SliderBounds
{
  int min = 0;
  int max = 0;
};
// --- Page 0 (Main Brewing Screen) ---
NextionComponent t_shotTime(nextion, 0, 13);
NextionComponent t_hxTemp(nextion, 0, 6);
NextionComponent t_boilerTemp(nextion, 0, 5);
NextionComponent pic_brew(nextion, 0, 1);
NextionComponent pic_boiler(nextion, 0, 2);
NextionComponent pic_arrow(nextion, 0, 3);
NextionComponent t_weight(nextion, 0, 15);
NextionComponent t_machineState(nextion, 0, 14);
int waveformID = 12;
NextionComponent wf_pressure(nextion, 0, waveformID);
NextionComponent btn_tare(nextion, 0, 16);
NextionComponent va_highlight(nextion, 0, 45);

// --- Page 1 (Main Settings) ---
NextionComponent t_hxTemp2(nextion, 1, 9);
NextionComponent t_boilerTemp2(nextion, 1, 6);
NextionComponent pic_brew2(nextion, 1, 1);
NextionComponent pic_boiler2(nextion, 1, 2);
NextionComponent pic_arrow2(nextion, 1, 3);
NextionComponent slider_brewTemp(nextion, 1, 23);
NextionComponent l_brewTemp(nextion, 1, 20);
NextionComponent l_brewMode(nextion, 1, 21);
NextionComponent l_steamBoost(nextion, 1, 22);
NextionComponent btn_brewModeCoffee(nextion, 1, 26);
NextionComponent btn_brewModeSteam(nextion, 1, 27);
NextionComponent btn_steamBoost(nextion, 1, 28);
NextionComponent x_brewTemp(nextion, 1, 24);

// --- Page 2 (Profiling Settings) ---
NextionComponent t_hxTemp3(nextion, 2, 9);
NextionComponent t_boilerTemp3(nextion, 2, 6);
NextionComponent pic_brew3(nextion, 2, 29);
NextionComponent pic_boiler3(nextion, 2, 30);
NextionComponent pic_arrow3(nextion, 2, 31);
NextionComponent btn_ModeManual(nextion, 2, 35);
NextionComponent btn_ModeFlat(nextion, 2, 36);
NextionComponent btn_ModeProfile(nextion, 2, 37);
NextionComponent btn_SourcePressure(nextion, 2, 38);
NextionComponent btn_SourceFlow(nextion, 2, 39);
NextionComponent btn_TargetTime(nextion, 2, 40);
NextionComponent btn_TargetWeight(nextion, 2, 41);
NextionComponent slt_Values(nextion, 2, 34);
NextionComponent t_profile(nextion, 2, 47);
NextionComponent sel_mode(nextion, 2, 33);
NextionComponent var_row(nextion, 2, 23);
NextionComponent var_col(nextion, 2, 24);
NextionComponent var_sel(nextion, 2, 43);
NextionComponent slt_flat(nextion, 2, 42);

// --- Page 3 (System settings) ---
NextionComponent t_hxTemp4(nextion, 3, 9);
NextionComponent t_boilerTemp4(nextion, 3, 6);
NextionComponent pic_brew4(nextion, 3, 1);
NextionComponent pic_boiler4(nextion, 3, 2);
NextionComponent pic_arrow4(nextion, 3, 3);
NextionComponent t_systemMessage(nextion, 3, 20);
NextionComponent btn_systemSettings(nextion, 3, 21);
NextionComponent btn_cleaningCycle(nextion, 3, 22);
NextionComponent btn_calibrateScale(nextion, 3, 23);
int referenceWeightID = 24;
NextionComponent x_referenceWeight(nextion, 3, referenceWeightID);
int referenceWeightUnitID = 25;
NextionComponent t_referenceWeightUnit(nextion, 3, referenceWeightUnitID);

// =================================================================
// --- NEXTION UI LAYOUT & MAPPINGS ---
// =================================================================
const int HIGHLIGHT_COLOR = 65519;
const int NUM_PAGE1_COMPONENTS = 3;
NextionComponent *page1_components[NUM_PAGE1_COMPONENTS][4] = {
    {&l_brewTemp, &slider_brewTemp, nullptr, &x_brewTemp},
    {&l_brewMode, &btn_brewModeCoffee, &btn_brewModeSteam, nullptr},
    {&l_steamBoost, &slider_brewTemp, nullptr, nullptr}};
SliderBounds page1_bounds[NUM_PAGE1_COMPONENTS];
int page1_cachedValues[NUM_PAGE1_COMPONENTS];

char valueString[1024];
char profilingName[128];
float flatValue;
bool isProfilingStepped;
uint16_t selectedItemPage2 = 1025;
uint8_t rowPage2 = 0;
uint8_t columnPage2 = 2;
int sltHeight = 0;

// =================================================================
// --- CHART & PLOTTING CONFIGURATION ---
// =================================================================
float const PRESSURE_MAX = 15;
float const PRESSURE_MIN = 0;
float const FLOW_RATE_MAX = 5.0;
float const FLOW_RATE_MIN = 0.0;

int chartWidth = 0;
int chartHeight = 0;
int plotPointsAdded = 0;

// =================================================================
// --- FORWARD DECLARATIONS ---
// =================================================================
// --- Setup & Core Loop ---
void setup();
void loop();

// --- Network & MQTT Functions ---
void setup_wifi();
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void publishData(const char *key, const char *value, bool esNowSendNow = true);
bool parseBool(const char *value);
void handleIncomingMessage(char *message);
void publishSetting();
void checkEncoderPublish();
void sendEspNowBuffer();
void sendPendingMqttSettings();
void resetPairing();
void manageSettingsRequests();

// --- Display & UI Functions ---
void updateDisplay();
void cacheSliderData();
void cacheEntriesData();
void cleanCurrentPage();
void updateChart();
void parseProfilingData();
void updateProfilingDisplay();
void updateFullProfileUI();
void saveProfile(int index);
void loadProfiles();
void saveCurrentProfileIndex();
void importProfileJson(const char *json);
void deleteProfile(int index);
bool isSlotFree(int index);
float getTargetAt(float currentX);

// --- Input Handling (Encoder & Button) ---
void knobCallback(long value);
void handleEncoder();
void buttonCallback(unsigned long duration);
void handleButton();

// --- Nextion Event Callbacks ---
void brewTempSliderRelease();
void brewModeButtonRelease();
void steamBoostButtonRelease();
void profilingModeManualButtonRelease();
void profilingModeFlatButtonRelease();
void profilingModeProfilingButtonRelease();
void profilingSourceButtonRelease();
void profilingTargetButtonRelease();
void profilingEntryFieldReleased(uint8_t id);
void systemSettingsButtonRelease();
void calibrateScaleButtonRelease();
void cleaningCycleButtonRelease();
void profilingTextRelease();
void profilingFlatRelease();
void profileNameRelease();
void profileSteppedRelease();
void buttonTareRelease();

// --- Webserver ---
void startProfilePortal();
void stopProfilePortal();
void handleProfileWebIndex();
void handleApiGetProfiles();
void handleApiSaveProfile();
void handleApiDeleteProfile();
void handleApiSetActiveProfile();
void handleRoot();
void setupWebRoutes();
void handleGlobalRoot();
void stopConfigurationPortal();

// --- Machine Logic & Simulation ---
void simulateShot();
int getShotTime(bool pumpStatus);

// --- Utility Functions ---
float mapf(float x, float in_min, float in_max, float out_min, float out_max);

// =================================================================
// --- FUNCTION DEFINITIONS ---
// =================================================================

// --- Setup & Core Loop ---
void setup()
{
  Serial.begin(115200);
  setupWebRoutes();
  rotaryEncoder.setEncoderType(EncoderType::FLOATING);
  rotaryEncoder.setBoundaries(ROTARY_MIN_BOUND, ROTARY_MAX_BOUND, true); // Example: Set target brew temp
  rotaryEncoder.onTurned(&knobCallback);
  rotaryEncoder.onPressed(&buttonCallback);
  rotaryEncoder.begin();
  lastEncoderValue = rotaryEncoder.getEncoderValue();

  Serial1.begin(9600, SERIAL_8N1, SERIAL_RX, SERIAL_TX);
  nextion.begin(Serial1, 9600);
  nextion.command("baud=19200");
  delay(1500);

  Serial1.begin(19200, SERIAL_8N1, SERIAL_RX, SERIAL_TX);
  nextion.begin(Serial1, 19200);

  if (!OFFLINE_MODE)
  {
    setup_wifi();
  }

  if (!OFFLINE_MODE)
  {
    myChannel = WiFi.channel();
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("Wi-Fi Channel: %d\n", myChannel);
    Serial.println("Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK)
    {
      Serial.println("Error initializing ESP-NOW");
      return;
    }
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      Serial.println("Failed to add broadcast peer");
    }
    else
    {
      Serial.println("Broadcast peer added for pairing.");
    }
    pairingData.id = PAIR_REQUEST;
    pairingData.channel = myChannel;
    WiFi.macAddress(pairingData.macAddr);
    strcpy(pairingData.identifier, espIdentifier);
    esp_now_send(broadcastAddress, (uint8_t *)&pairingData, sizeof(pairingData));
    Serial.println("Sent auto-pairing request...");
    lastPairingRequestTime = millis();

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  }
  va_highlight.value(HIGHLIGHT_COLOR);
  btn_steamBoost.release(steamBoostButtonRelease);
  slider_brewTemp.release(brewTempSliderRelease);
  btn_brewModeCoffee.release(brewModeButtonRelease);
  btn_brewModeSteam.release(brewModeButtonRelease);
  btn_ModeManual.release(profilingModeManualButtonRelease);
  btn_ModeFlat.release(profilingModeFlatButtonRelease);
  btn_ModeProfile.release(profilingModeProfilingButtonRelease);
  btn_SourcePressure.release(profilingSourceButtonRelease);
  btn_SourceFlow.release(profilingSourceButtonRelease);
  btn_TargetTime.release(profilingTargetButtonRelease);
  btn_TargetWeight.release(profilingTargetButtonRelease);
  btn_systemSettings.release(systemSettingsButtonRelease);
  btn_calibrateScale.release(calibrateScaleButtonRelease);
  btn_cleaningCycle.release(cleaningCycleButtonRelease);
  slt_Values.release(profilingTextRelease);
  slt_flat.release(profilingFlatRelease);
  t_profile.release(profileNameRelease);
  sel_mode.release(profileSteppedRelease);
  btn_tare.release(buttonTareRelease);

  nextion.command("sendme");
  delay(100);
  nextion.update();
  currentPage = nextion.getCurrentPageID();
  loadProfiles();
  updateFullProfileUI();
  cacheSliderData();
  cacheEntriesData();
  cleanCurrentPage();
}

void loop()
{
  if (Serial.available())
  {
    static char cmdBuffer[4096];
    size_t len = Serial.readBytesUntil('\n', cmdBuffer, sizeof(cmdBuffer) - 1);
    cmdBuffer[len] = '\0';

    if (len > 0 && cmdBuffer[len - 1] == '\r')
    {
      cmdBuffer[len - 1] = '\0';
      len--;
    }

    if (len > 0)
    {
      if (strncmp(cmdBuffer, "import_profile=", 15) == 0)
      {
        Serial.println("Serial command received: importing profile...");
        importProfileJson(cmdBuffer + 15);
      }
      else if (strncmp(cmdBuffer, "delete_profile=", 15) == 0)
      {
        int indexToDelete = atoi(cmdBuffer + 15);
        deleteProfile(indexToDelete);
      }
      else if (strncmp(cmdBuffer, "start_webserver", 15) == 0)
      {
        startProfilePortal();
      }
      else if (strncmp(cmdBuffer, "request", 7) == 0)
      {
        publishData("request", "settings", true);
      }
      else
      {
        Serial.print("Sending to Nextion: ");
        Serial.println(cmdBuffer);
        nextion.command(cmdBuffer);
      }
    }
  }

  if (profileIndexPendingSave && (millis() - profileIndexSettleTime > PROFILE_SAVE_DELAY_MS))
  {
    saveCurrentProfileIndex();
    profileIndexPendingSave = false;
    Serial.println("Settled: Saved new active profile index to NVS.");
  }

  if (chartWidth <= 0 || chartHeight <= 0)
  {
    Serial.println("Attempting to fetch chart dimensions...");
    chartWidth = wf_pressure.attributeValue("w");
    chartHeight = wf_pressure.attributeValue("h");
    if (chartWidth > 0)
    {
      Serial.printf("Chart dimensions fetched: W=%d, H=%d\n", chartWidth, chartHeight);
    }
    else
    {
      Serial.println("Failed to fetch chart dimensions yet...");
      chartWidth = 0;
      chartHeight = 0;
    }
  }
  if (!OFFLINE_MODE)
  {
    ArduinoOTA.handle();
    if (portalRunning || profileWebserverRunning)
    {
      server.handleClient();
    }
  }

  if (systemMessageClearTime > 0 && millis() > systemMessageClearTime)
  {
    if (currentPage == 3)
    {
      t_systemMessage.text("");
    }
    systemMessageClearTime = 0;
  }

  if (calibrationRequestTime > 0 && (millis() - calibrationRequestTime > CALIBRATION_REQUEST_TIMEOUT_MS))
  {
    Serial.println("Calibration request timed out.");
    calibrationStep = 0;
    calibrationRequestTime = 0;

    if (currentPage == 3)
    {
      t_systemMessage.text("Calibration Failed:\r\nTimeout");
    }
    systemMessageClearTime = millis() + 5000;

    char cmdBuffer[10];
    sprintf(cmdBuffer, "vis %d,0", referenceWeightID);
    nextion.command(cmdBuffer);
    sprintf(cmdBuffer, "vis %d,0", referenceWeightUnitID);
    nextion.command(cmdBuffer);
  }

  if (cleaningRequestTime > 0 && (millis() - cleaningRequestTime > CLEANING_REQUEST_TIMEOUT_MS))
  {
    Serial.println("Cleaning request timed out.");
    cleaningRequestTime = 0;
    cleaningCycleActive = false;
    cleaningCycleCount = 0;

    if (currentPage == 3)
    {
      t_systemMessage.text("Cleaning Failed:\r\nTimeout");
    }
    systemMessageClearTime = millis() + 5000;
  }

  if (isPaired && (millis() - lastControllerMessageTime > CONTROLLER_TIMEOUT_MS))
  {
    resetPairing();
  }
  int newCurrentPage = nextion.getCurrentPageID();
  if (newCurrentPage != currentPage)
  {
    currentPage = newCurrentPage;
    cleanCurrentPage();
  }
  if (newCurrentPage != 3 && portalRunning)
  {
    stopConfigurationPortal();
  }
  if (!OFFLINE_MODE && !isPaired)
  {
    if (millis() - lastPairingRequestTime > PAIRING_REQUEST_INTERVAL_MS)
    {
      Serial.println("Not paired. Retrying auto-pairing request...");

      pairingData.id = PAIR_REQUEST;
      pairingData.channel = myChannel;
      strcpy(pairingData.identifier, espIdentifier);
      WiFi.macAddress(pairingData.macAddr);

      esp_now_send(broadcastAddress, (uint8_t *)&pairingData, sizeof(pairingData));

      lastPairingRequestTime = millis();
    }
  }
  if (SIMULATION_MODE)
  {
    simulateShot();
  }
  if (!OFFLINE_MODE)
  {
    manageSettingsRequests();
  }

  nextion.update();
  handleEncoder();
  handleButton();
  checkEncoderPublish();
  shotTime = getShotTime(pumpIsOn);

  updateDisplay();
  updateChart();
  newCurrentPage = nextion.getCurrentPageID();
  if (newCurrentPage != currentPage)
  {
    currentPage = newCurrentPage;
  }
  delay(50);
  nextion.command("vis splash,0");
}

// --- Network & MQTT Functions ---
void setup_wifi()
{
  WiFi.mode(WIFI_STA);
  delay(100);

  WiFiManager wm;
  wm.setClass("invert");
  wm.setMinimumSignalQuality(30);
  wm.setConfigPortalTimeout(180);

  Serial.println("--- Wi-Fi Setup ---");
  Serial.println("Attempting to connect to saved Wi-Fi...");

  char apName[128];
  snprintf(apName, sizeof(apName), "%s-Setup", OTA_HOSTNAME);

  bool res = wm.autoConnect(apName);

  if (!res)
  {
    Serial.println("❌ FAILED to connect to Router (Timeout or wrong creds).");
  }
  else
  {
    Serial.println("✅ connected to Router!");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   Channel: ");
    Serial.println(WiFi.channel());
  }
  WiFi.setSleep(false);

  if (!MDNS.begin(OTA_HOSTNAME))
  {
    Serial.println("Error setting up MDNS responder!");
  }
  else
  {
    Serial.println("MDNS responder started: http://");
    Serial.print(OTA_HOSTNAME);
    Serial.println(".local");
    MDNS.addService("http", "tcp", 80);
  }
}

void handleRoot()
{
  server.sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server.sendHeader(F("Pragma"), F("no-cache"));
  server.sendHeader(F("Expires"), F("-1"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  static const char HTML_TOP[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Espresso HMI Config</title>
<style>body{font-family:sans-serif; background-color:#eee;} 
form{background-color:#fff; padding:20px; border-radius:5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);} 
label{display:block; margin-bottom:5px; font-weight:bold;} 
input[type='text'],input[type='number'],input[type='password']{width:95%; padding:8px; margin-bottom:10px; border:1px solid #ccc; border-radius:3px;} 
input[type='submit']{background-color:#4CAF50; color:white; padding:10px 15px; border:none; border-radius:3px; cursor:pointer;} 
input[type='submit']:hover{background-color:#45a049;} .msg{margin-top:15px; padding:10px; border-radius: 3px;}</style>
</head><body>
<h1>Configure Espresso Controller MQTT Settings</h1>
<form method='post' action='/save'>
)rawliteral";
  server.sendContent_P(HTML_TOP);
  char buffer[512];
  int len = snprintf(buffer, sizeof(buffer),
                     "<label for='server'>MQTT Server:</label><input type='text' name='server' value='%s'><br>"
                     "<label for='port'>MQTT Port:</label><input type='number' name='port' value='%s'><br>"
                     "<label for='user'>MQTT User:</label><input type='text' name='user' value='%s'><br>"
                     "<label for='pass'>MQTT Password:</label><input type='password' name='pass' value='%s'><br>"
                     "<input type='submit' value='Send Settings to Controller'></form>",
                     form_mqtt_server,
                     form_mqtt_port_str,
                     form_mqtt_user,
                     form_mqtt_password);
  server.sendContent(buffer, len);

  IPAddress ip = WiFi.localIP();
  len = snprintf(buffer, sizeof(buffer),
                 "<p>Current IP: %d.%d.%d.%d</p>"
                 "<p>Access via: <a href='http://%s.local'>http://%s.local</a></p>"
                 "</body></html>",
                 ip[0], ip[1], ip[2], ip[3],
                 OTA_HOSTNAME, OTA_HOSTNAME);

  server.sendContent(buffer, len);

  server.sendContent("");
  server.client().stop();
}

void handleGlobalRoot()
{
  if (portalRunning)
  {
    handleRoot();
  }
  else if (profileWebserverRunning)
  {
    handleProfileWebIndex();
  }
  else
  {
    server.send(404, "text/plain", "No active portal.");
  }
}

void handleSave()
{
  Serial.println("Handling save request...");
  bool changed = false;

  String newServer = server.hasArg("server") ? server.arg("server") : "";
  String newPortStr = server.hasArg("port") ? server.arg("port") : "";
  bool isNumeric = true;
  if (newPortStr.length() > 0)
  {
    for (unsigned int i = 0; i < newPortStr.length(); i++)
    {
      if (!isDigit(newPortStr.charAt(i)))
      {
        isNumeric = false;
        break;
      }
    }
  }
  if (newPortStr.isEmpty() || !isNumeric)
  {
    newPortStr = "1883";
    Serial.println("Invalid port detected, defaulting to 1883");
  }
  String newUser = server.hasArg("user") ? server.arg("user") : "";
  String newPass = server.hasArg("pass") ? server.arg("pass") : "";
  if (newPortStr.isEmpty())
  {
    newPortStr = "0";
  }
  if (newServer.compareTo(form_mqtt_server) != 0)
  {
    strncpy(form_mqtt_server, newServer.c_str(), sizeof(form_mqtt_server) - 1);
    form_mqtt_server[sizeof(form_mqtt_server) - 1] = '\0';
    changed = true;
    Serial.println("MQTT Server form value updated.");
  }

  if (newPortStr.compareTo(form_mqtt_port_str) != 0)
  {
    strncpy(form_mqtt_port_str, newPortStr.c_str(), sizeof(form_mqtt_port_str) - 1);
    form_mqtt_port_str[sizeof(form_mqtt_port_str) - 1] = '\0';
    changed = true;
    Serial.println("MQTT Port form value updated.");
  }

  if (newUser.compareTo(form_mqtt_user) != 0)
  {
    strncpy(form_mqtt_user, newUser.c_str(), sizeof(form_mqtt_user) - 1);
    form_mqtt_user[sizeof(form_mqtt_user) - 1] = '\0';
    changed = true;
    Serial.println("MQTT User form value updated.");
  }

  if (newPass.length() > 0 && newPass.compareTo(form_mqtt_password) != 0)
  {
    strncpy(form_mqtt_password, newPass.c_str(), sizeof(form_mqtt_password) - 1);
    form_mqtt_password[sizeof(form_mqtt_password) - 1] = '\0';
    changed = true;
    Serial.println("MQTT Password form value updated.");
  }
  else if (newPass.length() == 0 && form_mqtt_password[0] != '\0')
  {
    form_mqtt_password[0] = '\0';
    changed = true;
    Serial.println("MQTT Password form value cleared.");
  }

  if (changed)
  {
    Serial.println("Settings changed, marking as pending and attempting to send.");
    mqttSettingsPending = true;
    sendPendingMqttSettings();
  }
  else
  {
    Serial.println("No settings changed.");
  }
  server.send(200, F("text/html"), F("<!DOCTYPE html><html><head><title>Settings Sent</title><style>body{font-family:sans-serif; background-color:#eee; padding:20px;} .msg{background-color:#dff0d8; color: #3c763d; border: 1px solid #d6e9c6; padding:15px; border-radius:4px;}</style></head><body><div class='msg'>Settings sent to main controller! You can close this page.</div><p><a href='/'>Go Back</a></p></body></html>"));
  stopConfigurationPortal();
  t_systemMessage.text("MQTT Settings Sent!");
  systemMessageClearTime = millis() + 5000;
}

void setupWebRoutes()
{
  server.on("/", HTTP_GET, handleGlobalRoot);

  server.on("/save", HTTP_POST, handleSave);

  server.on("/api/profiles", HTTP_GET, handleApiGetProfiles);
  server.on("/api/profile", HTTP_POST, handleApiSaveProfile);
  server.on("/api/delete", HTTP_POST, handleApiDeleteProfile);
  server.on("/api/setactive", HTTP_POST, handleApiSetActiveProfile);
}

void startConfigurationPortal()
{
  if (portalRunning)
    return;
  if (profileWebserverRunning)
  {
    profileWebserverRunning = false;
  }
  Serial.println("Starting configuration web server...");
  portalRunning = true;
  server.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.println("HTTP server started (MQTT Config Mode).");
}

void stopConfigurationPortal()
{
  if (!portalRunning)
    return;
  t_systemMessage.text("");
  Serial.println("Stopping configuration web server...");
  server.stop();
  portalRunning = false;
  Serial.println("HTTP server stopped.");
  if (!OFFLINE_MODE && strcmp(profilingMode, "profile") == 0)
  {
    Serial.println("Restoring Profile Web Server...");
    startProfilePortal();
  }
  else
  {
    server.stop();
    Serial.println("HTTP server stopped.");
  }
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  if (len == sizeof(struct_pairing))
  {
    memcpy(&pairingData, incomingData, sizeof(pairingData));

    if (pairingData.id == PAIR_RESPONSE && !isPaired && strcmp(pairingData.identifier, espIdentifier) == 0)
    {
      Serial.print("Pairing response received from: ");
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
      Serial.println(macStr);

      memcpy(mainControllerMac, pairingData.macAddr, 6);
      int controllerChannel = pairingData.channel;
      esp_now_del_peer(broadcastAddress);

      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, mainControllerMac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;

      if (esp_now_add_peer(&peerInfo) != ESP_OK)
      {
        Serial.println("Failed to add main controller peer");
        return;
      }

      Serial.println("Successfully paired with main controller!");
      isPaired = true;

      sendPendingMqttSettings();

      Serial.println("Requesting current settings...");

      lastSettingsRequestTime = millis();
    }
  }
  else if (len == sizeof(struct_message))
  {
    if (!isPaired)
    {
      return;
    }

    if (memcmp(mac_addr, mainControllerMac, 6) != 0)
    {
      Serial.println("Data received from unknown MAC. Ignoring.");
      return;
    }
    lastControllerMessageTime = millis();
    if (SIMULATION_MODE)
    {
      return;
    }
    memcpy(&myData, incomingData, sizeof(myData));
    myData.payload[sizeof(myData.payload) - 1] = '\0';

    char *token = strtok(myData.payload, "|");
    while (token != NULL)
    {
      handleIncomingMessage(token);
      token = strtok(NULL, "|");
    }
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  if (status != ESP_NOW_SEND_SUCCESS)
  {
    Serial.println("ESP-NOW message failed to send.");
  }
}

void resetPairing()
{
  if (!isPaired)
    return;

  Serial.println("Resetting ESP-NOW pairing due to timeout or request.");
  isPaired = false;
  lastControllerMessageTime = 0;

  esp_now_del_peer(mainControllerMac);
  memset(mainControllerMac, 0, 6);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus != ESP_OK && addStatus != ESP_ERR_ESPNOW_EXIST)
  {
    Serial.printf("Error re-adding broadcast peer after reset: %s\n", esp_err_to_name(addStatus));
  }
  else
  {
    Serial.println("Broadcast peer active for re-pairing.");
  }

  lastPairingRequestTime = 0;
}

void manageSettingsRequests()
{
  static unsigned long lastRequestTime = 0;
  if (millis() - lastRequestTime < 500)
    return;

  if (!isPaired)
    return;

  if (!currentProfileReceived)
  {
    Serial.println("Requesting Profile...");
    publishData("request", "profile", true);
    lastRequestTime = millis();
    return;
  }

  if (!tempsetReceived)
  {
    publishData("request", "tempsetbrew", true);
    lastRequestTime = millis();
    return;
  }
  if (!brewModeReceived)
  {
    publishData("request", "brewmode", true);
    lastRequestTime = millis();
    return;
  }
  if (!steamBoostReceived)
  {
    publishData("request", "steamboost", true);
    lastRequestTime = millis();
    return;
  }

  if (!profilingModeReceived)
  {
    publishData("request", "prof_mode", true);
    lastRequestTime = millis();
    return;
  }
  if (!profilingSourceReceived)
  {
    publishData("request", "prof_src", true);
    lastRequestTime = millis();
    return;
  }
  if (!profilingTargetReceived)
  {
    publishData("request", "prof_trg", true);
    lastRequestTime = millis();
    return;
  }
  if (!flatValueReceived)
  {
    publishData("request", "prof_flat", true);
    lastRequestTime = millis();
    return;
  }
}

void sendPendingMqttSettings()
{
  if (!mqttSettingsPending)
  {
    return;
  }

  if (!isPaired)
  {
    Serial.println("Have pending MQTT settings, but not paired. Will wait.");
    return;
  }

  Serial.println("Sending pending MQTT config via ESP-NOW...");

  publishData("mqtt_server", form_mqtt_server, false);
  publishData("mqtt_port", form_mqtt_port_str, false);
  publishData("mqtt_user", form_mqtt_user, false);
  publishData("mqtt_password", form_mqtt_password, true);

  mqttSettingsPending = false;
  Serial.println("Pending MQTT config sent.");
}

void sendEspNowBuffer()
{
  if (strlen(espNowMessageBuffer) == 0)
    return;
  if (!isPaired)
    return;

  struct_message espnow_message;

  strncpy(espnow_message.payload, espNowMessageBuffer, sizeof(espnow_message.payload));
  espnow_message.payload[sizeof(espnow_message.payload) - 1] = '\0';

  esp_err_t result = esp_now_send(mainControllerMac, (uint8_t *)&espnow_message, sizeof(espnow_message));

  if (result == ESP_OK)
  {
    espNowMessageBuffer[0] = '\0';
  }
  else
  {
    Serial.println("ERROR: ESP-NOW send failed");
    espNowMessageBuffer[0] = '\0';
  }
}

void publishData(const char *topic, const char *payload, bool espNowSendNow)
{
  if (OFFLINE_MODE || !isPaired)
  {
    return;
  }
  char newEntry[250];
  snprintf(newEntry, sizeof(newEntry), "%s=%s", topic, payload);
  size_t currentLen = strlen(espNowMessageBuffer);
  size_t newEntryLen = strlen(newEntry);

  if (currentLen + newEntryLen + 2 > sizeof(myData.payload))
  {
    Serial.println("ESP-NOW buffer full, flushing automatically.");
    sendEspNowBuffer();
    currentLen = 0;
  }
  if (currentLen > 0)
  {
    strcat(espNowMessageBuffer, "|");
  }
  strcat(espNowMessageBuffer, newEntry);

  if (espNowSendNow)
  {
    sendEspNowBuffer();
  }
}

bool parseBool(const char *value)
{
  return (strcmp(value, "ON") == 0 || strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
}

void handleIncomingMessage(char *message)
{
  char *separator = strchr(message, '=');

  if (separator == NULL)
  {
    return;
  }
  *separator = '\0';
  char *key = message;
  char *value = separator + 1;

  if (strcmp(key, mqtt_topic_boiler_temp) == 0)
  {
    boilerTemp = atof(value);
  }
  else if (strcmp(key, mqtt_topic_hx_temp) == 0)
  {
    hxTemp = atof(value);
  }
  else if (strcmp(key, mqtt_topic_pressure) == 0)
  {
    pressure = atof(value);
  }
  else if (strcmp(key, mqtt_topic_heater) == 0)
  {
    isHeaterOn = parseBool(value);
  }
  else if (strcmp(key, mqtt_topic_pump) == 0)
  {
    pumpIsOn = parseBool(value);
  }
  else if (strcmp(key, mqtt_topic_import_profile) == 0)
  {
    currentProfileReceived = true;
    importProfileJson(value);
  }
  else if (strcmp(key, mqtt_topic_brew_mode) == 0)
  {
    brewModeReceived = true;
    bool isCoffee = (strcmp(value, "COFFEE") == 0);
    btn_brewModeCoffee.value(isCoffee ? 1 : 0);
    btn_brewModeSteam.value(isCoffee ? 0 : 1);
  }
  else if (strcmp(key, mqtt_topic_steam_boost_status) == 0)
  {
    steamBoostReceived = true;
    bool boostOn = (strcmp(value, "true") == 0);
    btn_steamBoost.value(boostOn ? 1 : 0);
  }
  else if (strcmp(key, mqtt_topic_state) == 0)
  {
    strncpy(machineState, value, sizeof(machineState) - 1);
    machineState[sizeof(machineState) - 1] = '\0';

    static char previousState[64] = "";
    char msgBuffer[250];

    if (strcmp(value, "CLEANING_START") == 0)
    {
      t_systemMessage.text("Cleaning Cycle:\r\nAdd detergent\r\nPull lever to start");
      cleaningCycleActive = true;
      cleaningCycleCount = 1;
      systemMessageClearTime = 0;
      cleaningRequestTime = 0;
    }
    else if (strcmp(value, "CLEANING_PUMPING") == 0)
    {
      if (strcmp(previousState, "CLEANING_PAUSE") == 0)
      {
        cleaningCycleCount++;
      }
      snprintf(msgBuffer, sizeof(msgBuffer), "Pumping... (Cycle %d)\r\nLower lever when buzzing", cleaningCycleCount);
      t_systemMessage.text(msgBuffer);
      cleaningCycleActive = true;
      systemMessageClearTime = 0;
      cleaningRequestTime = 0;
    }
    else if (strcmp(value, "CLEANING_PAUSE") == 0)
    {
      if (cleaningCycleCount < 5)
      {
        snprintf(msgBuffer, sizeof(msgBuffer), "Cleaning... (Cycle %d)\r\nLift lever", cleaningCycleCount);
      }
      else if (cleaningCycleCount == 5)
      {
        snprintf(msgBuffer, sizeof(msgBuffer), "Flushing... (Cycle %d)\r\nRemove detergent, lift lever", cleaningCycleCount);
      }
      else
      {
        snprintf(msgBuffer, sizeof(msgBuffer), "Flushing... (Cycle %d)\r\nLift lever", cleaningCycleCount);
      }
      t_systemMessage.text(msgBuffer);
      cleaningCycleActive = true;
      systemMessageClearTime = 0;
      cleaningRequestTime = 0;
    }
    else if (cleaningCycleActive && strstr(value, "CLEANING") == NULL && cleaningRequestTime == 0)
    {
      t_systemMessage.text("Cleaning Complete");
      cleaningCycleActive = false;
      cleaningCycleCount = 0;
      systemMessageClearTime = millis() + 5000;
    }
    else if (strcmp(value, "CALIBRATION_EMPTY") == 0)
    {
      if (calibrationStep == 10)
        return;
      calibrationRequestTime = 0;
      calibrationStep = 1;
      t_systemMessage.text("Calibration Started:\r\nEnsure scale is empty.\r\nPress button to tare");
    }
    else if (strcmp(value, "CALIBRATION_TEST_WEIGHT") == 0)
    {
      if (calibrationStep == 20)
        return;
      calibrationStep = 2;
      calibrationRequestTime = 0;
      t_systemMessage.text("Tare Complete.\r\nPlace weight on scale.\r\nPress button to weigh");

      char cmdBuffer[20];
      sprintf(cmdBuffer, "vis %d,1", referenceWeightID);
      nextion.command(cmdBuffer);
      sprintf(cmdBuffer, "vis %d,1", referenceWeightUnitID);
      nextion.command(cmdBuffer);
      nextion.command(cmdBuffer);
      delay(100);
      int screenVal = x_referenceWeight.value();
      if (screenVal != -1)
      {
        referenceWeight = (float)screenVal / 10.0f;
        Serial.printf("Default reference weight loaded: %.1fg\n", referenceWeight);
      }
      else
      {
        referenceWeight = 100.0f;
        x_referenceWeight.value(1000);
        Serial.println("Warning: Could not read weight from screen, using 100.0g");
      }
    }
    else if (calibrationStep != 0)
    {
      if (calibrationRequestTime > 0)
      {
        return;
      }
      else
      {
        if (strstr(previousState, "CALIBRATION") != NULL)
        {
          t_systemMessage.text("Calibration Complete");
        }
        else
        {
          t_systemMessage.text("Calibration Cancelled");
        }

        calibrationStep = 0;
        char cmdBuffer[20];
        sprintf(cmdBuffer, "vis %d,0", referenceWeightID);
        nextion.command(cmdBuffer);
        sprintf(cmdBuffer, "vis %d,0", referenceWeightUnitID);
        nextion.command(cmdBuffer);
        systemMessageClearTime = millis() + 5000;
      }
    }
    else if (cleaningCycleActive == false && portalRunning == false && calibrationRequestTime == 0 && cleaningRequestTime == 0)
    {
      if (systemMessageClearTime == 0)
      {
        t_systemMessage.text("");
      }
    }
    strncpy(previousState, value, sizeof(previousState) - 1);
    previousState[sizeof(previousState) - 1] = '\0';
  }
  else if (strcmp(key, mqtt_topic_lever) == 0)
  {
    brewLeverLifted = (strcmp(value, "LIFTED") == 0);
  }
  else if (strcmp(key, mqtt_topic_setpoint) == 0)
  {
    brewTempSetPoint = atof(value) * TEMP_SETPOINT_SCALE;
    int tempSetPointInt = (int)brewTempSetPoint;
    tempsetReceived = true;
    slider_brewTemp.value(tempSetPointInt);
    x_brewTemp.value(tempSetPointInt);
    page1_cachedValues[0] = tempSetPointInt;
  }
  else if (strcmp(key, mqtt_topic_set_profiling_mode) == 0)
  {
    profilingModeReceived = true;
    btn_ModeManual.value((strcmp(value, "manual") == 0) ? 1 : 0);
    btn_ModeFlat.value((strcmp(value, "flat") == 0) ? 1 : 0);
    btn_ModeProfile.value((strcmp(value, "profile") == 0) ? 1 : 0);

    strncpy(profilingMode, value, sizeof(profilingMode) - 1);
    profilingMode[sizeof(profilingMode) - 1] = '\0';

    if (strcmp(value, "manual") == 0)
    {
      nextion.command("click bt0,0");
      stopProfilePortal();
    }
    else if (strcmp(value, "flat") == 0)
    {
      nextion.command("click bt1,0");
      stopProfilePortal();
    }
    else if (strcmp(value, "profile") == 0)
    {
      nextion.command("click bt2,0");
      startProfilePortal();
    }
  }
  else if (strcmp(key, mqtt_topic_set_profiling_source) == 0)
  {
    profilingSourceReceived = true;
    strncpy(profilingSource, value, sizeof(profilingSource) - 1);
    profilingSource[sizeof(profilingSource) - 1] = '\0';
    bool isPressure = (strcmp(value, "pressure") == 0);
    btn_SourcePressure.value(isPressure ? 1 : 0);
    btn_SourceFlow.value(isPressure ? 0 : 1);
  }
  else if (strcmp(key, mqtt_topic_set_profiling_target) == 0)
  {
    profilingTargetReceived = true;
    strncpy(profilingTarget, value, sizeof(profilingTarget) - 1);
    profilingTarget[sizeof(profilingTarget) - 1] = '\0';
    bool isTime = (strcmp(value, "time") == 0);
    btn_TargetTime.value(isTime ? 1 : 0);
    btn_TargetWeight.value(isTime ? 0 : 1);
  }
  else if (strcmp(key, mqtt_topic_set_profiling_flat) == 0)
  {
    flatValue = atof(value);
    flatValueReceived = true;
    char newValStr[16];
    dtostrf(flatValue, 4, 1, newValStr);
    slt_flat.text(newValStr);
  }
  else if (strcmp(key, mqtt_topic_weight) == 0)
  {
    weight = atof(value);
  }
  else if (strcmp(key, mqtt_topic_flow_rate) == 0)
  {
    flowRate = atof(value);
  }
  else if (strcmp(key, mqtt_topic_mqtt_server) == 0)
  {
    strncpy(form_mqtt_server, value, sizeof(form_mqtt_server) - 1);
    form_mqtt_server[sizeof(form_mqtt_server) - 1] = '\0';
  }
  else if (strcmp(key, mqtt_topic_mqtt_port) == 0)
  {
    strncpy(form_mqtt_port_str, value, sizeof(form_mqtt_port_str) - 1);
    form_mqtt_port_str[sizeof(form_mqtt_port_str) - 1] = '\0';
  }
  else if (strcmp(key, mqtt_topic_mqtt_user) == 0)
  {
    strncpy(form_mqtt_user, value, sizeof(form_mqtt_user) - 1);
    form_mqtt_user[sizeof(form_mqtt_user) - 1] = '\0';
  }
  else if (strcmp(key, mqtt_topic_mqtt_pass) == 0)
  {
    strncpy(form_mqtt_password, value, sizeof(form_mqtt_password) - 1);
    form_mqtt_password[sizeof(form_mqtt_password) - 1] = '\0';
  }
  else if (strcmp(key, "raw_weight") == 0)
  {
    rawWeight = atof(value);
    lastDebugDataTime = millis();
  }
  else if (strcmp(key, "filtered_weight") == 0)
  {
    filteredWeight = atof(value);
    weight = filteredWeight;
    lastDebugDataTime = millis();
  }
  else if (strcmp(key, "filtered_flow") == 0)
  {
    filteredFlow = atof(value);
    flowRate = filteredFlow;
    lastDebugDataTime = millis();
  }
}

void publishSetting()
{
  if (pendingSettingIndex < 0)
    return;

  int value = page1_cachedValues[pendingSettingIndex];
  char numBuffer[10];

  switch (pendingSettingIndex)
  {
  case 0:
  {
    float valueF = (float)value / 10.0;
    dtostrf(valueF, 4, 3, numBuffer);
    publishData("tempsetbrew", numBuffer, true);
    break;
  }
  case SETTING_ID_FLAT_VALUE:
  {
    dtostrf(flatValue, 4, 1, numBuffer);
    Serial.print("Publishing flat value: ");
    Serial.println(numBuffer);
    publishData("profiling_flat_value", numBuffer, true);
    break;
  }
  case SETTING_ID_PROFILING_VALUE:
  {
    saveProfile(currentProfileIndex);
    char jsonBuffer[2048];
    JsonDocument doc;
    doc["n"] = currentProfile->name;
    doc["m"] = currentProfile->isStepped ? 1 : 0;
    JsonArray steps = doc["s"].to<JsonArray>();
    for (int i = 0; i < currentProfile->numSteps; i++)
    {
      JsonArray step = steps.add<JsonArray>();
      step.add(currentProfile->steps[i].target);
      step.add(currentProfile->steps[i].control);
    }

    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    Serial.print("Publishing Profile: ");
    Serial.println(jsonBuffer);

    publishData("profile_data", jsonBuffer, true);
    break;
  }
  }

  pendingSettingIndex = -1;
}

void checkEncoderPublish()
{
  if (pendingSettingIndex != -1 && (millis() - lastEncoderActivityTime > PUBLISH_TIMEOUT))
  {
    publishSetting();
  }
}

// --- Display & UI Functions ---
void updateDisplay()
{
  char buffer[10];

  if (shotTime != lastShotTime_sent)
  {
    if (shotTime == 0)
    {
      t_shotTime.text("");
      char cmdBuffer[32];
      sprintf(cmdBuffer, "cle %d,255", waveformID);
      nextion.command(cmdBuffer);
    }
    else
    {
      sprintf(buffer, "%d", shotTime);
      t_shotTime.text(buffer);
    }
    lastShotTime_sent = shotTime;
  }
  const int num_entries = 38;

  if (abs(hxTemp - lastHxTemp_sent) > 0.1)
  {
    dtostrf(hxTemp, 4, 1, buffer);
    t_hxTemp.text(buffer);
    t_hxTemp2.text(buffer);
    t_hxTemp3.text(buffer);
    t_hxTemp4.text(buffer);
    lastHxTemp_sent = hxTemp;
    int hxPic = round(map(hxTemp, 20, 100, 0, num_entries - 1));
    hxPic = constrain(hxPic, 0, num_entries - 1);
    pic_brew.attribute("pic", (int)hxPic);
    pic_brew2.attribute("pic", (int)hxPic);
    pic_brew3.attribute("pic", (int)hxPic);
    pic_brew4.attribute("pic", (int)hxPic);
  }

  if (abs(boilerTemp - lastBoilerTemp_sent) > 0.1)
  {
    dtostrf(boilerTemp, 4, 1, buffer);
    t_boilerTemp.text(buffer);
    t_boilerTemp2.text(buffer);
    t_boilerTemp3.text(buffer);
    t_boilerTemp4.text(buffer);
    lastBoilerTemp_sent = boilerTemp;
    int blPic = round(map(boilerTemp, 20, 140, num_entries, 2 * num_entries - 1));
    blPic = constrain(blPic, num_entries, 2 * num_entries - 1);
    pic_boiler.attribute("pic", (int)blPic);
    pic_boiler2.attribute("pic", (int)blPic);
    pic_boiler3.attribute("pic", (int)blPic);
    pic_boiler4.attribute("pic", (int)blPic);
  }
  if (abs(brewTempSetPoint - lastTempSetpoint_sent) > 0.1)
  {
    lastTempSetpoint_sent = brewTempSetPoint;
    int arrPic = round(map(brewTempSetPoint / 10, 20 - (100 - 20) / (num_entries - 2), 100 + (100 - 20) / (num_entries - 2), 2 * num_entries, 3 * num_entries));
    arrPic = constrain(arrPic, 2 * num_entries, 3 * num_entries - 1);
    pic_arrow.attribute("pic", (int)arrPic);
    pic_arrow2.attribute("pic", (int)arrPic);
    pic_arrow3.attribute("pic", (int)arrPic);
    pic_arrow4.attribute("pic", (int)arrPic);
  }

  if (abs(weight - lastWeight_sent) > 0.01)
  {
    sprintf(buffer, "%.1fg", weight);
    t_weight.text(buffer);
    lastWeight_sent = weight;
  }
  if (strcmp(machineState, lastMachineState_sent) != 0)
  {
    t_machineState.text(machineState);
    strlcpy(lastMachineState_sent, machineState, sizeof(lastMachineState_sent));
  }
}

void cacheSliderData()
{
  static bool rowCached[NUM_PAGE1_COMPONENTS] = {0};
  bool allCached = true;

  Serial.println("Caching slider data from Nextion...");

  for (int i = 0; i < NUM_PAGE1_COMPONENTS; i++)
  {
    if (rowCached[i])
    {
      continue;
    }

    NextionComponent *component1 = page1_components[i][1];
    NextionComponent *component2 = page1_components[i][2];
    NextionComponent *component3 = page1_components[i][3];

    if (component1 != nullptr && component2 == nullptr && component3 != nullptr)
    {

      int minVal = component1->attributeValue("minval");
      delay(10);
      int maxVal = component1->attributeValue("maxval");
      delay(10);
      int curVal = component1->value();

      if (minVal != -1 && maxVal != -1 && curVal != -1)
      {
        page1_bounds[i].min = minVal;
        page1_bounds[i].max = maxVal;
        page1_cachedValues[i] = curVal;
        rowCached[i] = true;
        Serial.printf("Row %d (Slider) cached: min=%d, max=%d, val=%d\n",
                      i, minVal, maxVal, curVal);
      }
      else
      {
        allCached = false;
        Serial.printf("Retrying row %d later (got -1)\n", i);
      }
    }
    else
    {
      rowCached[i] = true;
    }
  }

  if (!allCached)
  {
    delay(50);
    cacheSliderData();
  }
  else
  {
    Serial.println("All slider data successfully cached.");
  }
}

void cacheEntriesData()
{
  static bool valueStringLoaded = false;
  static bool flatValueLoaded = false;
  static bool profilingNameLoaded = false;
  static bool isProfilingSteppedLoaded = false;
  static bool selectedItemPage2Loaded = false;
  static bool rowPage2Loaded = false;
  static bool columnPage2Loaded = false;
  static bool sltHeightLoaded = false;

  Serial.println("Caching entries data from Nextion...");

  if (!valueStringLoaded)
  {
    const char *pValueString = slt_Values.text();
    delay(100);
    if (strcmp(pValueString, "Error") != 0)
    {
      strncpy(valueString, pValueString, sizeof(valueString) - 1);
      valueString[sizeof(valueString) - 1] = '\0';
      valueStringLoaded = true;
      parseProfilingData();
    }
    else
    {
      delay(50);
    }
  }

  if (!flatValueLoaded)
  {
    const char *pFlatString = slt_flat.text();
    delay(100);
    if (strcmp(pFlatString, "Error") != 0)
    {
      flatValue = atof(pFlatString);
      flatValueLoaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!profilingNameLoaded)
  {
    const char *pProfilingName = t_profile.text();
    delay(100);
    if (strcmp(pProfilingName, "Error") != 0)
    {
      strncpy(profilingName, pProfilingName, sizeof(profilingName) - 1);
      profilingName[sizeof(profilingName) - 1] = '\0';
      profilingNameLoaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!isProfilingSteppedLoaded)
  {
    int val = sel_mode.value();
    delay(100);
    if (val != -1)
    {
      isProfilingStepped = (val == 1);
      isProfilingSteppedLoaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!selectedItemPage2Loaded)
  {
    int val = var_sel.value();
    delay(100);
    if (val != -1)
    {
      selectedItemPage2 = val;
      selectedItemPage2Loaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!rowPage2Loaded)
  {
    int val = var_row.value();
    delay(100);
    if (val != -1)
    {
      rowPage2 = val;
      rowPage2Loaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!columnPage2Loaded)
  {
    int val = var_col.value();
    delay(100);
    if (val != -1)
    {
      columnPage2 = val;
      columnPage2Loaded = true;
    }
    else
    {
      delay(50);
    }
  }

  if (!sltHeightLoaded)
  {
    int val = slt_Values.attributeValue("h");
    delay(100);
    if (val != -1)
    {
      sltHeight = val;
      sltHeightLoaded = true;
      Serial.print("slt height loaded: ");
      Serial.println(sltHeight);
    }
    else
    {
      delay(50);
    }
  }

  if (!valueStringLoaded || !flatValueLoaded || !profilingNameLoaded || !isProfilingSteppedLoaded || !selectedItemPage2Loaded || !rowPage2Loaded || !columnPage2Loaded || !sltHeightLoaded)
  {
    cacheEntriesData();
  }
  else
  {
    Serial.println("All entries data successfully cached.");
  }
}

void cleanCurrentPage()
{
  switch (currentPage)
  {
  case 1:
    for (int i = 0; i < NUM_PAGE1_COMPONENTS; i++)
    {
      page1_components[i][0]->attribute("font", 6);
    }
    break;
  case 3:
    if (calibrationStep == 0 && !cleaningCycleActive)
    {
      t_systemMessage.text("");
    }
    x_referenceWeight.value(1000);
    if (calibrationStep != 2)
    {
      char cmdBuffer[10];
      sprintf(cmdBuffer, "vis %d,0", referenceWeightID);
      nextion.command(cmdBuffer);
      sprintf(cmdBuffer, "vis %d,0", referenceWeightUnitID);
      nextion.command(cmdBuffer);
    }
  default:
    break;
  }
  isItemSelected = false;
}

void updateChart()
{
  char cmdBuffer[32];
  if (chartHeight <= 0 || chartWidth <= 0)
  {
    return;
  }
  if (shotStartTimeMillis > 0 && (chartStopTime == 0 || millis() < chartStopTime))
  {
    if (!shotIsActive)
    {
      shotIsActive = true;
      plotPointsAdded = 0;
      sprintf(cmdBuffer, "cle %d,255", waveformID);
      nextion.command(cmdBuffer);
    }
    unsigned long elapsedShotMillis = millis() - shotStartTimeMillis;
    int targetPixelCount = mapf(elapsedShotMillis, 0, MAX_SHOT_TIME_S * 1000, 0, chartWidth);

    while (plotPointsAdded < targetPixelCount)
    {
      uint8_t scaledPressure = round(mapf(pressure, PRESSURE_MIN, PRESSURE_MAX, 0, (float)chartHeight));
      sprintf(cmdBuffer, "add %d,0,%d", waveformID, (int)scaledPressure);
      nextion.command(cmdBuffer);

      uint8_t scaledFlowRate = round(mapf(flowRate, FLOW_RATE_MIN, FLOW_RATE_MAX, 0, (float)chartHeight));
      sprintf(cmdBuffer, "add %d,1,%d", waveformID, (int)scaledFlowRate);
      nextion.command(cmdBuffer);

      bool drawChannel2 = false;
      int scaledTarget = 0;
      float targetY = 0.0f;
      if (strcmp(profilingMode, "flat") == 0)
      {
        if (strcmp(profilingSource, "pressure") == 0)
        {
          scaledTarget = round(mapf(flatValue, PRESSURE_MIN, PRESSURE_MAX, 0, (float)chartHeight));
        }
        else
        {
          scaledTarget = round(mapf(flatValue, FLOW_RATE_MIN, FLOW_RATE_MAX, 0, (float)chartHeight));
        }
        scaledTarget = constrain(scaledTarget, 0, 255);
        drawChannel2 = true;
      }
      else if (strcmp(profilingMode, "profile") == 0)
      {
        float currentX = 0.0f;
        if (strcmp(profilingTarget, "time") == 0)
        {
          float pixelTime = mapf(plotPointsAdded, 0, chartWidth, 0, MAX_SHOT_TIME_S);
          currentX = pixelTime;
        }
        else
        {
          currentX = weight;
        }

        targetY = getTargetAt(currentX);
        if (strcmp(profilingSource, "pressure") == 0)
        {
          scaledTarget = round(mapf(targetY, PRESSURE_MIN, PRESSURE_MAX, 0, (float)chartHeight));
        }
        else
        {
          scaledTarget = round(mapf(targetY, FLOW_RATE_MIN, FLOW_RATE_MAX, 0, (float)chartHeight));
        }
        drawChannel2 = true;
      }
      if (drawChannel2)
      {
        sprintf(cmdBuffer, "add %d,2,%d", waveformID, scaledTarget);
        nextion.command(cmdBuffer);
      }

      plotPointsAdded++;
    }
  }
  else
  {
    if (shotIsActive)
    {
      shotIsActive = false;
      shotStartTimeMillis = 0;
    }

    bool isDebugActive = (millis() - lastDebugDataTime < DEBUG_PLOT_TIMEOUT_MS);

    if (isDebugActive)
    {
      static unsigned long lastIdlePlotTime = 0;
      if (millis() - lastIdlePlotTime > 100)
      {
        lastIdlePlotTime = millis();

        uint8_t scaledRawWeight = round(mapf(rawWeight, DEBUG_WEIGHT_MIN, DEBUG_WEIGHT_MAX, 0, (float)chartHeight));
        sprintf(cmdBuffer, "add %d,0,%d", waveformID, (int)scaledRawWeight);
        nextion.command(cmdBuffer);

        uint8_t scaledFilteredWeight = round(mapf(filteredWeight, DEBUG_WEIGHT_MIN, DEBUG_WEIGHT_MAX, 0, (float)chartHeight));
        sprintf(cmdBuffer, "add %d,1,%d", waveformID, (int)scaledFilteredWeight);
        nextion.command(cmdBuffer);

        uint8_t scaledFilteredFlow = round(mapf(filteredFlow, DEBUG_FLOW_MIN, DEBUG_FLOW_MAX, 0, (float)chartHeight));
        sprintf(cmdBuffer, "add %d,2,%d", waveformID, (int)scaledFilteredFlow);
        nextion.command(cmdBuffer);
      }
    }
  }
}

float getTargetAt(float currentX)
{
  if (currentProfile == nullptr || currentProfile->numSteps == 0)
    return 0.0f;

  float stepStartX = 0.0f;
  float prevTargetY = 0.0f;

  for (int i = 0; i < currentProfile->numSteps; i++)
  {
    float duration = currentProfile->steps[i].control;
    float targetY = currentProfile->steps[i].target;

    if (duration <= 0.001f)
    {
      prevTargetY = targetY;
      continue;
    }

    float stepEndX = stepStartX + duration;

    if (currentX <= stepEndX)
    {
      if (currentProfile->isStepped)
      {
        return targetY;
      }
      else
      {
        return (currentX - stepStartX) * (targetY - prevTargetY) / duration + prevTargetY;
      }
    }

    stepStartX = stepEndX;
    prevTargetY = targetY;
  }

  return prevTargetY;
}

// --- Input Handling (Encoder & Button) ---
void knobCallback(long value)
{
  const long range = (ROTARY_MAX_BOUND - ROTARY_MIN_BOUND + 1);
  const long halfRange = range / 2;

  long diff = value - lastEncoderValue;
  int direction = 0;

  if (diff > halfRange)
  {
    direction = -1;
  }
  else if (diff < -halfRange)
  {
    direction = 1;
  }
  else if (diff > 0)
  {
    direction = 1;
  }
  else if (diff < 0)
  {
    direction = -1;
  }

  if (direction != 0)
  {
    portENTER_CRITICAL(&encoderMux);
    encoderTicks += direction;
    portEXIT_CRITICAL(&encoderMux);
  }
  lastEncoderValue = value;
}

void handleEncoder()
{
  if (currentPage == 2 && selectedItemPage2 > 1024)
    return;
  long ticksToProcess = 0;
  if (encoderTicks != 0)
  {
    portENTER_CRITICAL(&encoderMux);
    ticksToProcess = encoderTicks;
    encoderTicks = 0;
    portEXIT_CRITICAL(&encoderMux);
  }

  if (ticksToProcess == 0)
  {
    return;
  }

  int selectedIndex = currentSelectionIndex - 1;

  switch (currentPage)
  {
  case 1:
  {
    int selectedIndexPage1 = 0;
    NextionComponent *componentFloat = page1_components[selectedIndexPage1][3];
    NextionComponent *component1 = page1_components[selectedIndexPage1][1];
    NextionComponent *component2 = page1_components[selectedIndexPage1][2];

    int currentValue = page1_cachedValues[selectedIndexPage1];
    int minVal = page1_bounds[selectedIndexPage1].min;
    int maxVal = page1_bounds[selectedIndexPage1].max;

    int step = 1;
    int totalChange = ticksToProcess * step;
    int newValue = constrain(currentValue + totalChange, minVal, maxVal);
    if (newValue != currentValue)
    {
      component1->value(newValue);
      page1_cachedValues[selectedIndexPage1] = newValue;
      componentFloat->value(newValue);
      pendingSettingIndex = selectedIndexPage1;
      lastEncoderActivityTime = millis();
    }

    break;
  }
  case 2:
  {
    if ((strcmp(profilingMode, "flat") == 0) && selectedItemPage2 == 0)
    {
      flatValue = flatValue + (ticksToProcess * 0.1f);
      if (flatValue < 0)
        flatValue = 0;
      char newValStr[16];
      dtostrf(flatValue, 4, 1, newValStr);
      slt_flat.text(newValStr);
      pendingSettingIndex = SETTING_ID_FLAT_VALUE;
      lastEncoderActivityTime = millis();
    }
    else if (strcmp(profilingMode, "profile") == 0)
    {
      if (selectedItemPage2 == 1024)
      {
        if (currentProfileDirty)
        {
          Serial.printf("Saving changes to profile %d before switching...\n", currentProfileIndex);
          saveProfile(currentProfileIndex);
          currentProfileDirty = false;
        }

        int direction = (ticksToProcess > 0) ? 1 : -1;
        int attempts = 0;
        int nextIndex = currentProfileIndex;
        do
        {
          nextIndex += direction;
          if (nextIndex >= MAX_PROFILES)
            nextIndex = 0;
          if (nextIndex < 0)
            nextIndex = MAX_PROFILES - 1;
          attempts++;
        } while (profiles[nextIndex].numSteps == 0 && attempts < MAX_PROFILES);

        if (profiles[nextIndex].numSteps > 0 && nextIndex != currentProfileIndex)
        {
          currentProfileIndex = nextIndex;

          profileIndexSettleTime = millis();
          profileIndexPendingSave = true;

          currentProfile = &profiles[currentProfileIndex];
          updateFullProfileUI();
          currentProfileDirty = false;
        }

        lastEncoderActivityTime = millis();
        pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
      }
      else if (rowPage2 < currentProfile->numSteps && selectedItemPage2 < 1024)
      {
        float change = ticksToProcess * 0.1f;

        if (columnPage2 == 0)
        {
          currentProfile->steps[rowPage2].target += change;
        }
        else if (columnPage2 == 1)
        {
          currentProfile->steps[rowPage2].control += change;
        }
        currentProfileDirty = true;
        updateProfilingDisplay();
        lastEncoderActivityTime = millis();
        pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
      }
    }
    break;
  }
  case 3:
  {
    x_referenceWeight.attribute("bco", HIGHLIGHT_COLOR);
    float currentFloatValue = referenceWeight;
    float step = 0.5f;
    float totalChange = (float)ticksToProcess * step;
    float newFloatValue = currentFloatValue + totalChange;
    newFloatValue = constrain(newFloatValue, 0.0f, 500.0f);

    if (newFloatValue != currentFloatValue)
    {
      referenceWeight = newFloatValue;
      x_referenceWeight.value((int)(newFloatValue * 10.0f));

      lastEncoderActivityTime = millis();
    }
    break;
  }
  }
}

void parseProfilingData()
{
  currentProfile->numSteps = 0;
  char tempString[1024];
  strncpy(tempString, valueString, sizeof(tempString) - 1);
  tempString[sizeof(tempString) - 1] = '\0';

  const char *delimiters = " \n\r";
  char *token = strtok(tempString, delimiters);
  int floatCount = 0;

  while (token != NULL && currentProfile->numSteps < 128)
  {
    float val = atof(token);

    if (floatCount % 2 == 0)
    {
      currentProfile->steps[currentProfile->numSteps].target = val;
    }
    else
    {
      currentProfile->steps[currentProfile->numSteps].control = val;
      currentProfile->numSteps++;
    }

    floatCount++;
    token = strtok(NULL, delimiters);
  }

  strncpy(currentProfile->name, profilingName, sizeof(currentProfile->name) - 1);
  currentProfile->isStepped = isProfilingStepped;

  Serial.printf("Profile Parsed: '%s' (%s), %d steps loaded.\n",
                currentProfile->name,
                currentProfile->isStepped ? "Stepped" : "Ramped",
                currentProfile->numSteps);
}

void loadProfiles()
{
  Serial.println("--- LOADING PROFILES FROM NVS ---");

  preferences.begin("profiles", true);
  currentProfileIndex = preferences.getInt("curIdx", 0);
  preferences.end();

  preferences.begin("profs_data", true);
  int firstValidIndex = -1;

  for (int i = 0; i < MAX_PROFILES; i++)
  {
    char key[10];
    sprintf(key, "p_%d", i);
    size_t len = preferences.getBytesLength(key);

    if (len == sizeof(EspressoProfile))
    {
      preferences.getBytes(key, &profiles[i], sizeof(EspressoProfile));
      if (profiles[i].numSteps > 0)
      {
        if (firstValidIndex == -1)
          firstValidIndex = i;
        Serial.printf("[Slot %d] FOUND: '%s' (%d steps)\n", i, profiles[i].name, profiles[i].numSteps);
      }
      else
      {
        profiles[i].numSteps = 0;
      }
    }
    else
    {
      profiles[i].numSteps = 0;
    }
  }
  preferences.end();

  if (firstValidIndex == -1)
  {
    Serial.println("NOTE: Memory completely empty. Creating 'Standard Profile' in Slot 0.");
    profiles[0].numSteps = 1;
    profiles[0].steps[0] = {9.0, 30.0};
    strcpy(profiles[0].name, "Standard Profile");
    profiles[0].isStepped = false;
    firstValidIndex = 0;
  }

  if (currentProfileIndex < 0 || currentProfileIndex >= MAX_PROFILES || profiles[currentProfileIndex].numSteps == 0)
  {
    Serial.printf("Warning: Index %d is empty or invalid. Switching to first valid slot: %d\n", currentProfileIndex, firstValidIndex);
    currentProfileIndex = firstValidIndex;
    saveCurrentProfileIndex();
  }

  currentProfile = &profiles[currentProfileIndex];
  Serial.printf("--- LOAD COMPLETE. Active: '%s' (Slot %d) ---\n", currentProfile->name, currentProfileIndex);
}

void deleteProfile(int index)
{
  if (index < 0 || index >= MAX_PROFILES)
  {
    Serial.printf("Error: Profile index %d out of range (0-%d)\n", index, MAX_PROFILES - 1);
    return;
  }

  if (profiles[index].numSteps == 0 && strlen(profiles[index].name) == 0)
  {
    return;
  }

  Serial.printf("Deleting profile %d ('%s')...\n", index, profiles[index].name);

  profiles[index].name[0] = '\0';
  profiles[index].isStepped = false;
  profiles[index].numSteps = 0;
  memset(profiles[index].steps, 0, sizeof(profiles[index].steps));

  saveProfile(index);

  if (index == currentProfileIndex)
  {
    Serial.println("Active profile was deleted. Resetting to index 0.");
    int newIndex = -1;
    for (int i = 0; i < MAX_PROFILES; i++)
    {
      if (profiles[i].numSteps > 0)
      {
        newIndex = i;
        break;
      }
    }

    if (newIndex == -1)
    {
      Serial.println("All profiles empty. Creating default safety profile at index 0.");
      newIndex = 0;
      strcpy(profiles[0].name, "Standard Profile");
      profiles[0].numSteps = 1;
      profiles[0].steps[0] = {9.0, 30.0};
      profiles[0].isStepped = false;
      saveProfile(0);
    }

    currentProfileIndex = newIndex;
    saveCurrentProfileIndex();
    currentProfile = &profiles[currentProfileIndex];
    updateFullProfileUI();
  }

  Serial.printf("Profile %d deleted successfully.\n", index);
}

void saveCurrentProfileIndex()
{
  preferences.begin("profiles", false);
  preferences.putInt("curIdx", currentProfileIndex);
  preferences.end();
}

void saveProfile(int index)
{
  if (index < 0 || index >= MAX_PROFILES)
    return;
  preferences.begin("profs_data", false);
  char key[10];
  sprintf(key, "p_%d", index);
  preferences.putBytes(key, &profiles[index], sizeof(EspressoProfile));
  preferences.end();
  Serial.printf("Profile %d ('%s') saved to NVS.\n", index, profiles[index].name);
}

void updateFullProfileUI()
{
  updateProfilingDisplay();
  t_profile.text(currentProfile->name);
  sel_mode.value(currentProfile->isStepped ? 1 : 0);
  strncpy(profilingName, currentProfile->name, sizeof(profilingName) - 1);
  isProfilingStepped = currentProfile->isStepped;
}

bool isSlotFree(int index)
{
  return (profiles[index].numSteps == 0);
}

void importProfileJson(const char *json)
{
  Serial.println("Attempting to import profile...");

  EspressoProfile tempProfile;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error)
  {
    Serial.print("Profile JSON parsing failed: ");
    Serial.println(error.c_str());
    if (currentPage == 3)
    {
      t_systemMessage.text("Import Failed:\r\nInvalid JSON");
      systemMessageClearTime = millis() + 5000;
    }
    return;
  }

  strlcpy(tempProfile.name, doc["n"] | "Imported Profile", sizeof(tempProfile.name));
  tempProfile.isStepped = (doc["m"] == 1);

  JsonArray steps = doc["s"];
  tempProfile.numSteps = 0;
  for (JsonVariant step : steps)
  {
    if (tempProfile.numSteps >= 128)
      break;
    tempProfile.steps[tempProfile.numSteps].target = step[0].as<float>();
    tempProfile.steps[tempProfile.numSteps].control = step[1].as<float>();
    tempProfile.numSteps++;
  }

  int targetIndex = -1;
  int firstFreeIndex = -1;

  for (int i = 0; i < MAX_PROFILES; i++)
  {
    if (strcmp(profiles[i].name, tempProfile.name) == 0)
    {
      targetIndex = i;
      Serial.printf("Found existing profile '%s' at index %d. Updating.\n", tempProfile.name, i);
      break;
    }

    if (firstFreeIndex == -1 && isSlotFree(i))
    {
      firstFreeIndex = i;
    }
  }

  if (targetIndex == -1)
  {
    if (firstFreeIndex != -1)
    {
      targetIndex = firstFreeIndex;
      Serial.printf("Name '%s' not found. Inserting into free slot %d.\n", tempProfile.name, targetIndex);
    }
    else
    {
      Serial.println("Import failed: Profile list is full and no existing name matched.");
      if (currentPage == 3)
      {
        t_systemMessage.text("Import Failed:\r\nProfile Memory Full");
        systemMessageClearTime = millis() + 5000;
      }
      return;
    }
  }

  profiles[targetIndex] = tempProfile;
  saveProfile(targetIndex);

  if (targetIndex == currentProfileIndex)
  {
    currentProfile = &profiles[targetIndex];
    updateFullProfileUI();
    Serial.println("Active profile updated via import.");
  }

  if (currentPage == 3)
  {
    char msg[50];
    sprintf(msg, "Import Successful:\r\n%s", tempProfile.name);
    t_systemMessage.text(msg);
    systemMessageClearTime = millis() + 5000;
  }
}

void updateProfilingDisplay()
{
  char outputBuffer[1024] = "";
  char floatBuffer[20];

  for (int i = 0; i < currentProfile->numSteps; i++)
  {
    if (currentProfile->steps[i].target < 0)
      currentProfile->steps[i].target = 0;
    if (currentProfile->steps[i].target > 999.9)
      currentProfile->steps[i].target = 999.9;

    if (currentProfile->steps[i].target < 9.99)
    {
      dtostrf(currentProfile->steps[i].target, 7, 1, floatBuffer);
    }
    else if (currentProfile->steps[i].target < 99.99)
    {
      dtostrf(currentProfile->steps[i].target, 6, 1, floatBuffer);
    }
    else
    {
      dtostrf(currentProfile->steps[i].target, 5, 1, floatBuffer);
    }
    strcat(outputBuffer, floatBuffer);
    strcat(outputBuffer, "           ");

    if (currentProfile->steps[i].control < 0)
      currentProfile->steps[i].control = 0;
    if (currentProfile->steps[i].control > 999.9)
      currentProfile->steps[i].control = 999.9;

    if (currentProfile->steps[i].control < 9.99)
    {
      dtostrf(currentProfile->steps[i].control, 7, 1, floatBuffer);
    }
    else if (currentProfile->steps[i].control < 99.99)
    {
      dtostrf(currentProfile->steps[i].control, 6, 1, floatBuffer);
    }
    else
    {
      dtostrf(currentProfile->steps[i].control, 5, 1, floatBuffer);
    }
    strcat(outputBuffer, floatBuffer);

    if (i < currentProfile->numSteps - 1)
    {
      strcat(outputBuffer, "\r\n");
    }
  }

  strncpy(valueString, outputBuffer, sizeof(valueString) - 1);
  valueString[sizeof(valueString) - 1] = '\0';
  slt_Values.text(valueString);
}

void buttonCallback(unsigned long duration)
{
  buttonPressed = true;
}

void handleButton()
{
  if (!buttonPressed)
  {
    return;
  }
  buttonPressed = false;
  bool pageChanged = lastPageForSelection != currentPage;
  if (pageChanged)
  {
    currentSelectionIndex = 0;
  }
  switch (currentPage)
  {
  // case 1:
  //   currentSelectionIndex = 0;
  //   if (currentSelectionIndex >= NUM_PAGE1_COMPONENTS) {
  //     currentSelectionIndex = 0;
  //   }

  //   for (int i = 0; i < NUM_PAGE1_COMPONENTS; i++) {
  //     if (i == currentSelectionIndex) {
  //       page1_components[i][0]->attribute("font", 2);
  //     } else {
  //       page1_components[i][0]->attribute("font", 6);
  //     }
  //   }
  //   lastPageForSelection = currentPage;
  //   currentSelectionIndex++;
  //   isItemSelected = true;
  //   break;
  case 2:
  {
    if (strcmp(profilingMode, "manual") == 0)
    {
      break;
    }
    else if (strcmp(profilingMode, "flat") == 0)
    {
      var_row.value(0);
      var_col.value(0);
      var_sel.value(0);
      selectedItemPage2 = 0;
      rowPage2 = 0;
      columnPage2 = 0;
      slt_flat.attribute("bco", HIGHLIGHT_COLOR);
      break;
    }
    else if (strcmp(profilingMode, "profile") == 0)
    {
      const int val_y = slt_Values.attributeValue("val_y");
      delay(50);
      int maxIndex = (currentProfile->numSteps * 2 - 1);
      if (selectedItemPage2 >= 1024)
      {
        var_col.value(0);
        var_sel.value(0);
        var_row.value(0);
        selectedItemPage2 = 0;
        columnPage2 = 0;
        rowPage2 = 0;
        slt_Values.attribute("val_y", 0);
        t_profile.attribute("bco", 65536);
        break;
      }
      if (selectedItemPage2 >= maxIndex)
      {
        selectedItemPage2 = 1024;
        columnPage2 = 2;
        rowPage2 = 0;
        var_col.value(columnPage2);
        var_row.value(rowPage2);
        var_sel.value(selectedItemPage2);
        t_profile.attribute("bco", HIGHLIGHT_COLOR);
        break;
      }
      t_profile.attribute("bco", 65536);
      selectedItemPage2++;
      var_sel.value(selectedItemPage2);
      if (columnPage2 == 0)
      {
        columnPage2 = 1;
        var_col.value(columnPage2);
      }
      else if (columnPage2 == 1)
      {
        rowPage2++;
        columnPage2 = 0;
        var_col.value(columnPage2);
        var_row.value(rowPage2);
      }
      if ((1 + rowPage2) * 20 - val_y >= sltHeight)
      {
        slt_Values.attribute("val_y", (1 + rowPage2) * 12);
      }
    }
    break;
  }
  case 3:
  {
    if (calibrationStep == 2)
    {
      isItemSelected = true;
      x_referenceWeight.attribute("bco", HIGHLIGHT_COLOR);
      int screenVal = x_referenceWeight.value();
      if (screenVal != -1)
      {
        referenceWeight = (float)screenVal / 10.0f;
      }
    }
    else
    {
      isItemSelected = false;
    }
  }
  break;
  default:
    isItemSelected = false;
    break;
  }
}

// --- Nextion Event Callbacks ---
void brewTempSliderRelease()
{
  delay(100);
  int value = slider_brewTemp.value();
  float valueF = (float)value / 10.;
  page1_cachedValues[0] = value;
  char numBuffer[10];
  dtostrf(valueF, 4, 3, numBuffer);

  char payloadBuffer[32];
  sprintf(payloadBuffer, "tempsetbrew=%s", numBuffer);

  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("tempsetbrew", numBuffer, true);
}

void brewModeButtonRelease()
{
  delay(100);
  int value = btn_brewModeCoffee.value();
  char payloadBuffer[32];

  if (value == 0)
  {
    sprintf(payloadBuffer, "steam");
  }
  else
  {
    sprintf(payloadBuffer, "coffee");
  }

  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("brewmode", payloadBuffer, true);
}

void steamBoostButtonRelease()
{
  delay(100);
  int value = btn_steamBoost.value();
  char payloadBuffer[32];
  if (value == 1)
  {
    sprintf(payloadBuffer, "true");
  }
  else
  {
    sprintf(payloadBuffer, "false");
  }

  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("enablesteamboost", payloadBuffer, true);
}

void profilingModeManualButtonRelease()
{
  char payloadBuffer[32];
  cleanCurrentPage();
  strlcpy(profilingMode, "manual", sizeof(profilingMode));
  sprintf(payloadBuffer, "manual");
  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("profiling_mode", payloadBuffer, true);
  stopProfilePortal();
}

void profilingModeFlatButtonRelease()
{
  delay(100);
  const char *pFlatString = slt_flat.text();
  delay(100);
  if (strcmp(pFlatString, "Error") != 0)
  {
    flatValue = atof(pFlatString);
  }
  char payloadBuffer[32];
  cleanCurrentPage();
  strlcpy(profilingMode, "flat", sizeof(profilingMode));
  sprintf(payloadBuffer, "flat");
  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("profiling_mode", payloadBuffer, true);
  stopProfilePortal();
}

void profilingModeProfilingButtonRelease()
{
  delay(100);
  const char *pValueString = slt_Values.text();
  delay(100);
  if (strcmp(pValueString, "Error") != 0)
  {
    strncpy(valueString, pValueString, sizeof(valueString) - 1);
    valueString[sizeof(valueString) - 1] = '\0';
    parseProfilingData();
  }
  char payloadBuffer[32];
  cleanCurrentPage();
  strlcpy(profilingMode, "profile", sizeof(profilingMode));
  sprintf(payloadBuffer, "profile");
  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("profiling_mode", payloadBuffer, true);
  pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
  startProfilePortal();
}

void profilingSourceButtonRelease()
{
  delay(100);
  char payloadBuffer[32];

  if (btn_SourcePressure.value() == 1)
  {
    sprintf(payloadBuffer, "pressure");
    strlcpy(profilingSource, "pressure", sizeof(profilingSource));
  }
  else
  {
    sprintf(payloadBuffer, "flow");
    strlcpy(profilingSource, "flow", sizeof(profilingSource));
  }

  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("profiling_source", payloadBuffer, true);
}

void systemSettingsButtonRelease()
{
  Serial.println("System Settings Button Pressed.");
  if (calibrationStep != 0 || cleaningCycleActive)
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi Connected. Starting MQTT Config Portal for Main Controller.");

    char msgBuffer[150];
    IPAddress ip = WiFi.localIP();

    snprintf(msgBuffer, sizeof(msgBuffer),
             "Espresso Controller MQTT Config:\r\nhttp://%d.%d.%d.%d\nor\r\nhttp://%s.local",
             ip[0], ip[1], ip[2], ip[3],
             OTA_HOSTNAME);

    t_systemMessage.text(msgBuffer);
    startConfigurationPortal();
  }
  else
  {
    Serial.println("WiFi Not Connected. Cannot start MQTT portal.");
    t_systemMessage.text("Error:\r\nWiFi not connected");
    systemMessageClearTime = millis() + 5000;
  }
}

void cleaningCycleButtonRelease()
{
  Serial.println("Cleaning Cycle button pressed.");

  if (portalRunning || calibrationStep != 0)
  {
    return;
  }

  if (!isPaired)
  {
    t_systemMessage.text("Error:\r\nNot connected to\r\nmain controller");
    systemMessageClearTime = millis() + 5000;
    return;
  }

  if (cleaningCycleActive)
  {
    return;
  }

  t_systemMessage.text("Cleaning cycle requested...");
  cleaningCycleCount = 0;
  systemMessageClearTime = 0;
  cleaningRequestTime = millis();

  Serial.println("Sending cleaning=start command");
  publishData("start_cleaning", "true", true);
}

void profilingTextRelease()
{
  delay(100);
  const int row_val = var_row.value();
  delay(100);
  const int col_val = var_col.value();
  delay(100);
  const int sel_val = var_sel.value();
  delay(100);
  const char *xValString = slt_Values.attributeText("txt");
  bool rerun = false;
  if (row_val == -1)
    rerun = true;
  if (col_val == -1)
    rerun = true;
  if (sel_val == -1)
    rerun = true;
  if (strcmp(xValString, "Error") == 0)
    rerun = true;
  if (rerun)
  {
    profilingTextRelease();
    return;
  }
  strncpy(valueString, xValString, sizeof(valueString) - 1);
  parseProfilingData();
  selectedItemPage2 = sel_val;
  rowPage2 = row_val;
  columnPage2 = col_val;
}

void profilingFlatRelease()
{
  delay(50);
  const char *xValString = slt_flat.attributeText("txt");
  delay(100);
  var_row.value(0);
  var_col.value(0);
  var_sel.value(0);
  selectedItemPage2 = 0;
  rowPage2 = 0;
  columnPage2 = 0;
  flatValue = atof(xValString);
}

void profileNameRelease()
{
  delay(100);
  var_col.value(2);
  var_sel.value(1024);
  selectedItemPage2 = 1024;
  columnPage2 = 2;
  strncpy(currentProfile->name, profilingName, sizeof(currentProfile->name) - 1);
  const char *pProfilingName = t_profile.text();
  if (strcmp(pProfilingName, "Error") != 0)
  {
    strncpy(profilingName, pProfilingName, sizeof(profilingName) - 1);
    profilingName[sizeof(profilingName) - 1] = '\0';
    strncpy(currentProfile->name, profilingName, sizeof(currentProfile->name) - 1);
    currentProfileDirty = true;
  }
}

void profileSteppedRelease()
{
  delay(100);
  int val = sel_mode.value();
  bool oldProfilingIsStepped = isProfilingStepped;
  if (val != -1)
  {
    isProfilingStepped = (val == 1);
  }
  currentProfile->isStepped = isProfilingStepped;
  if (oldProfilingIsStepped != isProfilingStepped)
  {
    currentProfileDirty = true;
    pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
    publishSetting();
  }
}

void buttonTareRelease()
{
  delay(100);
  publishData("tare_scale", "true", true);
}

void calibrateScaleButtonRelease()
{
  Serial.println("Calibrate Scale button pressed.");
  if (portalRunning || cleaningCycleActive)
  {
    return;
  }
  if (!isPaired)
  {
    t_systemMessage.text("Error:\r\nNot connected to\r\nmain controller");
    systemMessageClearTime = millis() + 5000;
    return;
  }

  systemMessageClearTime = 0;
  char cmdBuffer[10];

  if (calibrationStep == 0)
  {
    Serial.println("Sending calibratescale command");
    publishData("calibratescale", "", true);

    t_systemMessage.text("Calibration requested...");
    calibrationRequestTime = millis();

    sprintf(cmdBuffer, "vis %d,0", referenceWeightID);
    nextion.command(cmdBuffer);
    sprintf(cmdBuffer, "vis %d,0", referenceWeightUnitID);
    nextion.command(cmdBuffer);
  }
  else if (calibrationStep == 1)
  {
    Serial.println("Sending calibration_step=0.0 (for tare).");
    publishData("calibration_step", "0.0", true);
    t_systemMessage.text("Taring scale...\r\nPlease wait...");
    calibrationStep = 10;
  }
  else if (calibrationStep == 2)
  {
    float realWeight = referenceWeight;
    if (realWeight <= 0)
    {
      t_systemMessage.text("Error: Set reference weight\r\nusing encoder first (e.g., 100)");
      systemMessageClearTime = millis() + 5000;
      return;
    }

    char weightBuffer[10];
    snprintf(weightBuffer, sizeof(weightBuffer), "%.1f", realWeight);

    sprintf(cmdBuffer, "vis %d,0", referenceWeightID);
    nextion.command(cmdBuffer);
    sprintf(cmdBuffer, "vis %d,0", referenceWeightUnitID);
    nextion.command(cmdBuffer);

    Serial.printf("Sending calibration_step=%s (for weight)\n", weightBuffer);
    publishData("calibration_step", weightBuffer, true);

    t_systemMessage.text("Weighing... Finishing calibration");
    calibrationStep = 20;
  }
}

void profilingTargetButtonRelease()
{
  delay(100);
  char payloadBuffer[32];

  if (btn_TargetTime.value() == 1)
  {
    sprintf(payloadBuffer, "time");
    strlcpy(profilingTarget, "time", sizeof(profilingTarget));
  }
  else
  {
    sprintf(payloadBuffer, "weight");
    strlcpy(profilingTarget, "weight", sizeof(profilingTarget));
  }

  Serial.print("Publishing payload: ");
  Serial.println(payloadBuffer);
  publishData("profiling_target", payloadBuffer, true);
}

void profilingEntryFieldReleased(uint8_t id)
{
  isItemSelected = true;
  currentSelectionIndex = id + 1;
  lastPageForSelection = currentPage;
}

// --- Machine Logic & Simulation ---

void simulateShot()
{
  static bool simulationActive = false;
  static unsigned long simulationStartTime = 0;
  const int simulationDuration = 30; // seconds
  const int preinfusionTime = 4;     // seconds
  const float targetYield = 56.0;    // grams

  unsigned long currentTime = millis();

  if (!simulationActive && shotTime == 0)
  {
    simulationActive = true;
    simulationStartTime = currentTime;
    pumpIsOn = true;
    brewLeverLifted = true;
    weight = 0.0f;
    flowRate = 0.0f;
  }

  brewTempSetPoint = 0;
  strlcpy(machineState, "BREWING", sizeof(machineState));
  if (simulationActive)
  {
    unsigned long elapsedMillis = currentTime - simulationStartTime;
    int elapsedSeconds = elapsedMillis / 1000;
    hxTemp = 112;
    boilerTemp = 154.5;
    if (elapsedSeconds < preinfusionTime)
    {
      pressure = map(elapsedSeconds, 0, preinfusionTime, 0.0, 9.0);
      weight = 0.0f;
    }
    else
    {
      pressure = 9.0 + (random(-30, 31) / 100.0);

      float extractionDurationSeconds = simulationDuration - preinfusionTime;
      float avgFlowRate = targetYield / extractionDurationSeconds;

      unsigned long extractionMillis = elapsedMillis - (preinfusionTime * 1000);
      float extractionTimeSeconds = (float)extractionMillis / 1000.0f;

      if (extractionDurationSeconds > 0)
      {
        weight = extractionTimeSeconds * avgFlowRate + (random(-10, 11) / 100.0);
        weight = max(0.0f, weight);
      }
      flowRate = avgFlowRate + (random(-20, 21) / 100.0);
      flowRate = max(0.0f, flowRate);
    }

    if (elapsedSeconds >= simulationDuration)
    {
      pumpIsOn = false;
      brewLeverLifted = false;
      pressure = 0.0;
      flowRate = 0.0;
      simulationActive = false;
    }
  }
}

int getShotTime(bool pumpStatus)
{
  static unsigned long pumpStartTime = 0;
  static int completedShotTime = 0;
  static unsigned long leverLoweredTime = 0;

  unsigned long currentTime = millis();
  if (pumpStatus && brewLeverLifted)
  {
    leverLoweredTime = 0;
    chartStopTime = 0;
    if (pumpStartTime == 0)
    {
      pumpStartTime = currentTime;
      shotStartTimeMillis = currentTime;
      completedShotTime = 0;
    }
    unsigned long duration = currentTime - pumpStartTime;
    return (duration > SHOT_START_THRESHOLD_S * 1000) ? duration / 1000 : 0;
  }
  else
  {
    if (pumpStartTime != 0)
    {
      unsigned long duration = currentTime - pumpStartTime;
      completedShotTime = (duration > SHOT_START_THRESHOLD_S * 1000) ? duration / 1000 : 0;
      pumpStartTime = 0;
      if (completedShotTime > 0)
      {
        leverLoweredTime = currentTime;
        chartStopTime = millis() + CHART_RETENTION_TIME_MS;
      }
    }
    if (leverLoweredTime != 0 && (currentTime - leverLoweredTime > SHOT_RETENTION_TIME_MS))
    {
      completedShotTime = 0;
      leverLoweredTime = 0;
    }
    return completedShotTime;
  }
}

// --- PROFILE WEB SERVER FUNCTIONS ---

void handleProfileWebIndex()
{
  Serial.println("DEBUG: Serving HTML Index...");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  static const char PROFILE_HTML[] PROGMEM = R"=====(<!doctype html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta content="width=device-width, initial-scale=1, maximum-scale=1" name="viewport">
    <title>MaraX Evolution Profile Editor</title>
    <style>
        :root {
            --bg: #1a1a1a;
            --fg: #e0e0e0;
            --accent: #d4a017;
            --danger: #c62828;
            --card: #2d2d2d;
            --input-bg: #444;
            --border: #555;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: var(--bg);
            color: var(--fg);
            margin: 0;
            padding: 10px;
            font-size: 16px;
        }

        h1,
        h2,
        h3 {
            color: var(--accent);
            margin-top: 0;
        }

        h1 {
            font-size: 1.5rem;
            text-align: center;
            margin-bottom: 20px;
        }

        .container {
            max-width: 800px;
            margin: 0 auto;
        }

        .card {
            background: var(--card);
            padding: 15px;
            border-radius: 12px;
            margin-bottom: 15px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, .2);
        }

        .btn {
            min-width: 80px;
            padding: 10px 16px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-weight: 700;
            font-size: 0.9rem;
            transition: opacity .2s, transform .1s;
            color: #fff;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            min-height: 40px;
        }

        #editName {
            font-size: 1rem;
            padding: 5px;
            box-sizing: border-box;
            width: 100%;
            height: 40px;
        }

        .btn:active {
            transform: scale(0.98);
        }

        .btn:hover {
            opacity: .9;
        }

        .btn-primary {
            background: var(--accent);
            color: #000;
        }

        .btn-danger {
            background: var(--danger);
        }

        .btn-secondary {
            background: #555;
        }

        input[type=number],
        input[type=text],
        select {
            background: var(--input-bg);
            border: 1px solid var(--border);
            color: var(--fg);
            padding: 10px;
            border-radius: 6px;
            width: 100%;
            box-sizing: border-box;
            font-size: 16px;
            appearance: none;
        }

        input:focus {
            outline: 2px solid var(--accent);
            border-color: transparent;
        }

        .header-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 10px;
            margin-bottom: 15px;
        }

        .row {
            display: flex;
            gap: 15px;
            flex-wrap: wrap;
            margin-bottom: 15px;
        }

        .col {
            flex: 1;
            min-width: 200px;
        }

        label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
            font-size: 0.9em;
            color: #aaa;
        }

        .active-badge {
            background: var(--accent);
            color: #000;
            padding: 3px 8px;
            border-radius: 12px;
            font-size: 0.75em;
            font-weight: bold;
            margin-left: 8px;
            text-transform: uppercase;
        }

        .hidden {
            display: none !important;
        }

        table {
            width: 100%;
            border-collapse: separate;
            border-spacing: 0;
        }

        th {
            text-align: left;
            padding: 12px 8px;
            border-bottom: 2px solid #444;
            color: #aaa;
        }

        td {
            padding: 12px 8px;
            border-bottom: 1px solid #444;
            vertical-align: middle;
        }

        .editor-header {
            display: flex;
            gap: 10px;
            margin-bottom: 15px;
            align-items: center;
        }

        @media screen and (max-width: 700px) {

            #profileList table,
            #profileList thead,
            #profileList tbody,
            #profileList th,
            #profileList td,
            #profileList tr {
                display: block;
            }

            .editor-header {
                flex-direction: column;
                align-items: stretch;
                gap: 15px;
            }

            .editor-header>div,
            .mode-select-container {
                width: 100%;
                flex-basis: auto;
            }

            .btn {
                min-height: 36px;
            }

            #editName,
            #editMode {
                width: 100%;
            }

            #profileList thead tr {
                position: absolute;
                top: -9999px;
                left: -9999px;
            }

            #profileList tr {
                margin-bottom: 15px;
                background: #363636;
                border-radius: 8px;
                padding: 10px;
                border: 1px solid #444;
            }

            #profileList td {
                border: none;
                padding: 5px 0;
                position: relative;
            }

            #profileList td:last-child {
                display: flex;
                gap: 5px;
                margin-top: 10px;
                padding-top: 10px;
                border-top: 1px solid #444;
            }

            #profileList .btn {
                flex: 1;
            }

            #stepsTable thead {
                display: none;
            }

            #stepsTable tr {
                display: grid;
                grid-template-columns: 1fr 1fr 50px;
                gap: 10px;
                background: #363636;
                padding: 10px;
                margin-bottom: 10px;
                border-radius: 8px;
                align-items: end;
            }

            #stepsTable tr.step-row {
                display: grid;
                grid-template-columns: 25px 1fr 1fr 40px;
                gap: 5px;
                align-items: center;
                padding: 5px 0;
            }

            #stepsTable td {
                padding: 0 !important;
                border: none !important;
                display: block;
            }

            #stepsTable input {
                padding: 6px 2px;
                font-size: 14px;
                height: 36px;
            }

            #stepsTable .btn-danger {
                width: 100%;
                min-width: unset;
                padding: 0;
                height: 36px;
            }

            #stepsTable td {
                display: block;
                border: none;
                padding: 0;
            }

            #stepsTable td:nth-child(2):before {
                content: "Target";
                display: block;
                font-size: 0.8em;
                color: #aaa;
                margin-bottom: 2px;
            }

            #stepsTable td:nth-child(3):before {
                content: "Control";
                display: block;
                font-size: 0.8em;
                color: #aaa;
                margin-bottom: 2px;
            }

            #stepsTable td:first-child {
                grid-column: 1 / -1;
                font-weight: bold;
                color: var(--accent);
                margin-bottom: 5px;
            }

            #stepsTable td:first-child:before {
                content: "Step ";
            }
        }

        #status {
            text-align: center;
            font-weight: bold;
            margin-bottom: 20px;
            color: #000;
            padding: 10px;
            border-radius: 6px;
        }

        .editor-actions {
            display: flex;
            gap: 10px;
            margin-top: 20px;
        }

        .editor-actions .btn {
            flex: 1;
        }
    </style>
</head>

<body>
    <div class="container">
        <h1>☕ MaraX Profile Manager</h1>
        <div class="hidden" id="status"></div>
        <div id="listView">
            <div class="card">
                <div class="header-row">
                    <h2>Available Profiles</h2>
                    <button class="btn btn-secondary" onclick="fetchProfiles()">↻ Refresh</button>
                </div>
                <div id="profileList">Loading...</div>
            </div>
        </div>
        <div class="hidden" id="editorView">
            <div class="card">
                <h2 id="editorTitle">Edit Profile</h2>
                <div class="row">
                    <div class="col">
                        <label>Profile Name</label>
                        <input id="editName" maxlength="64" placeholder="e.g. Turbo Bloom">
                    </div>
                    <div class="col">
                        <label>Mode</label>
                        <select id="editMode">
                            <option value="0">Ramped</option>
                            <option value="1">Stepped</option>
                        </select>
                    </div>
                </div>
                <h3>Profile Steps</h3>
                <table id="stepsTable">
                    <thead>
                        <tr>
                            <th style="width: 50px">#</th>
                            <th>Target (bar / g/s)</th>
                            <th>Control (s / g)</th>
                            <th style="width: 60px"></th>
                        </tr>
                    </thead>
                    <tbody id="stepsBody"></tbody>
                </table>
                <button class="btn btn-secondary" style="width: 100%; margin-top: 10px; border-style: dashed;"
                    onclick="addStepRow()">+ Add Step</button>
                <div class="editor-actions">
                    <button class="btn btn-secondary" onclick="showList()">Cancel</button>
                    <button class="btn btn-primary" onclick="saveCurrentProfile()">Save Profile</button>
                </div>
            </div>
        </div>
    </div>
    <script>
        let profiles = [];
        let currentEditIndex = -1;
        async function fetchProfiles() {
            try {
                const res = await fetch('/api/profiles?_=' + Date.now());
                if (!res.ok) throw new Error("Failed to fetch");
                const data = await res.json();
                profiles = data.profiles;
                renderList(data.active);
                if (currentEditIndex !== -1 && !document.getElementById('editorView').classList.contains('hidden')) {
                    editProfile(currentEditIndex);
                }
            } catch (e) {
                showStatus('Error fetching profiles: ' + e.message, true);
            }
        }
        function renderList(activeIndex) {
            const listEl = document.getElementById('profileList');
            listEl.innerHTML = '';
            if (profiles.length === 0) {
                listEl.innerHTML = '<div style="text-align:center; padding:20px; color:#666">No profiles found</div>';
                return;
            }
            const table = document.createElement('table');
            table.innerHTML = '<thead><tr><th>Slot</th><th>Name</th><th>Steps</th><th>Actions</th></tr></thead><tbody></tbody>';
            const tbody = table.querySelector('tbody');
            profiles.forEach((p, idx) => {
                const isEmpty = (p.s.length === 0);
                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td>Slot ${idx} ${idx === activeIndex ? '<span class="active-badge">ACTIVE</span>' : ''}</td>
                    <td><strong style="${isEmpty ? 'color:#777; font-style:italic' : ''}">${isEmpty ? 'Empty Slot' : p.n}</strong></td>
                    <td>${isEmpty ? '-' : p.s.length + ' steps'}</td>
                    <td>
                        <div style="display:flex; gap:5px;">
                            <button class="btn btn-primary" onclick="editProfile(${idx})">
                                ${isEmpty ? 'Create' : 'Edit'}
                            </button>
                            <button class="btn btn-secondary" onclick="setActive(${idx})" 
                                ${(idx === activeIndex || isEmpty) ? 'disabled' : ''}>Load</button>
                            <button class="btn btn-danger" onclick="deleteProfile(${idx})"
                                ${isEmpty ? 'disabled' : ''}>Clear</button>
                        </div>
                    </td>
                `;
                tbody.appendChild(tr);
            });
            listEl.appendChild(table);
        }
        function editProfile(index) {
            currentEditIndex = index;
            const p = profiles[index];
            document.getElementById('editorTitle').innerText = `Editing Slot ${index}`;
            document.getElementById('editName').value = p.n;
            document.getElementById('editMode').value = p.m;
            renderSteps(p.s);
            document.getElementById('listView').classList.add('hidden');
            document.getElementById('editorView').classList.remove('hidden');
        }
        function renderSteps(steps) {
            const tbody = document.getElementById('stepsBody');
            tbody.innerHTML = '';
            steps.forEach((step) => {
                let t = Array.isArray(step) ? step[0] : step.t;
                let c = Array.isArray(step) ? step[1] : step.c;
                addStepRow(t, c);
            });
        }
        function addStepRow(target = 9.0, control = 10.0) {
            const tbody = document.getElementById('stepsBody');
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><span class="step-idx"></span></td>
                <td><input type="number" step="0.1" class="step-target" value="${target}"></td>
                <td><input type="number" step="0.1" class="step-control" value="${control}"></td>
                <td><button class="btn btn-danger" style="padding: 5px 10px;" onclick="this.closest('tr').remove(); updateStepIndices();">✕</button></td>
            `;
            tbody.appendChild(tr);
            updateStepIndices();
        }
        function updateStepIndices() {
            document.querySelectorAll('#stepsBody tr').forEach((tr, i) => {
                tr.querySelector('.step-idx').innerText = i + 1;
            });
        }
        function showList() {
            currentEditIndex = -1;
            document.getElementById('editorView').classList.add('hidden');
            document.getElementById('listView').classList.remove('hidden');
            fetchProfiles();
        }
        async function saveCurrentProfile() {
            const steps = [];
            document.querySelectorAll('#stepsBody tr').forEach(tr => {
                steps.push([
                    parseFloat(tr.querySelector('.step-target').value) || 0,
                    parseFloat(tr.querySelector('.step-control').value) || 0
                ]);
            });
            const payload = {
                id: currentEditIndex,
                n: document.getElementById('editName').value,
                m: parseInt(document.getElementById('editMode').value),
                s: steps
            };
            try {
                const res = await fetch('/api/profile', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                if (res.ok) {
                    showStatus('Profile saved successfully!', false);
                    showList();
                } else {
                    showStatus('Save failed', true);
                }
            } catch (e) {
                showStatus('Error saving: ' + e.message, true);
            }
        }
        async function setActive(index) {
            try {
                await fetch(`/api/setactive?index=${index}`, { method: 'POST' });
                fetchProfiles();
                showStatus(`Profile ${index} loaded.`, false);
            } catch (e) {
                showStatus('Error: ' + e.message, true);
            }
        }
        async function deleteProfile(index) {
            if (!confirm(`Are you sure you want to clear slot ${index}?`)) return;
            try {
                await fetch(`/api/delete?index=${index}`, { method: 'POST' });
                fetchProfiles();
                showStatus(`Profile ${index} cleared.`, false);
            } catch (e) {
                showStatus('Error: ' + e.message, true);
            }
        }
        function showStatus(msg, isError) {
            const el = document.getElementById('status');
            el.innerText = msg;
            el.style.background = isError ? 'var(--danger)' : 'var(--accent)';
            el.style.color = isError ? '#fff' : '#000';
            el.classList.remove('hidden');
            setTimeout(() => el.classList.add('hidden'), 3000);
        }
        fetchProfiles();
    </script>
</body>

</html>)=====";
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  size_t len = strlen_P(PROFILE_HTML);
  Serial.printf("DEBUG: HTML size is %d bytes\n", len);

  size_t sent = 0;
  const size_t CHUNK_SIZE = 512;
  char chunk[CHUNK_SIZE + 1];

  while (sent < len)
  {
    size_t toCopy = (len - sent > CHUNK_SIZE) ? CHUNK_SIZE : (len - sent);

    memcpy_P(chunk, PROFILE_HTML + sent, toCopy);

    server.sendContent(chunk, toCopy);
    sent += toCopy;

    delay(2);
  }

  server.sendContent("");
  server.client().stop();
}

void handleApiGetProfiles()
{
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);

  server.send(200, "application/json", "{\"active\":");
  char numBuffer[16];
  snprintf(numBuffer, sizeof(numBuffer), "%d", currentProfileIndex);
  server.sendContent(numBuffer);
  server.sendContent(",\"profiles\":[");

  JsonDocument doc;
  char jsonOutputBuffer[512];

  for (int i = 0; i < MAX_PROFILES; i++)
  {
    doc.clear();

    doc["id"] = i;
    doc["n"] = profiles[i].name;
    doc["m"] = profiles[i].isStepped ? 1 : 0;

    JsonArray steps = doc["s"].to<JsonArray>();
    for (int j = 0; j < profiles[i].numSteps; j++)
    {
      JsonArray step = steps.add<JsonArray>();
      step.add(profiles[i].steps[j].target);
      step.add(profiles[i].steps[j].control);
    }

    serializeJson(doc, jsonOutputBuffer, sizeof(jsonOutputBuffer));

    server.sendContent(jsonOutputBuffer);

    if (i < MAX_PROFILES - 1)
    {
      server.sendContent(",");
    }
  }

  server.sendContent("]}");
  server.client().stop();
}

void handleApiSaveProfile()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, F("text/plain"), F("Body missing"));
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg(F("plain")));

  if (error)
  {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  int id = doc["id"] | -1;
  if (id < 0 || id >= MAX_PROFILES)
  {
    server.send(400, "text/plain", "Invalid Profile ID");
    return;
  }

  strlcpy(profiles[id].name, doc["n"] | "Unnamed", sizeof(profiles[id].name));
  profiles[id].isStepped = (doc["m"] == 1);

  JsonArray steps = doc["s"];
  profiles[id].numSteps = 0;
  for (JsonVariant s : steps)
  {
    if (profiles[id].numSteps >= 128)
      break;
    if (s.is<JsonArray>())
    {
      profiles[id].steps[profiles[id].numSteps].target = s[0].as<float>();
      profiles[id].steps[profiles[id].numSteps].control = s[1].as<float>();
    }
    else if (s.is<JsonObject>())
    {
      profiles[id].steps[profiles[id].numSteps].target = s["t"] | 0.0f;
      profiles[id].steps[profiles[id].numSteps].control = s["c"] | 1.0f;
    }
    profiles[id].numSteps++;
  }

  saveProfile(id);

  if (id == currentProfileIndex)
  {
    currentProfile = &profiles[id];
    updateFullProfileUI();
    pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
    publishSetting();
  }

  server.send(200, "application/json", "{\"success\":true}");
}

void handleApiDeleteProfile()
{
  if (!server.hasArg("index"))
  {
    server.send(400, "text/plain", "Index missing");
    return;
  }
  int index = server.arg("index").toInt();
  deleteProfile(index);
  server.send(200, "application/json", "{\"success\":true}");
}

void handleApiSetActiveProfile()
{
  if (!server.hasArg("index"))
  {
    server.send(400, "text/plain", "Index missing");
    return;
  }
  int index = server.arg("index").toInt();
  if (index >= 0 && index < MAX_PROFILES && profiles[index].numSteps > 0)
  {
    currentProfileIndex = index;
    saveCurrentProfileIndex();
    currentProfile = &profiles[index];
    updateFullProfileUI();
    pendingSettingIndex = SETTING_ID_PROFILING_VALUE;
    publishSetting();
    server.send(200, "application/json", "{\"success\":true}");
  }
  else
  {
    server.send(400, "text/plain", "Invalid profile index");
  }
}

void startProfilePortal()
{
  if (OFFLINE_MODE || profileWebserverRunning)
    return;
  if (portalRunning)
  {
    portalRunning = false;
  }

  Serial.println("Starting Profile Web Server...");
  profileWebserverRunning = true;

  server.begin();
  Serial.println("☕ Profile Editor Web Server Started!");
}

void stopProfilePortal()
{
  if (!profileWebserverRunning)
    return;
  Serial.println("Stopping Profile Web Server...");
  server.stop();
  profileWebserverRunning = false;
}

// --- Utility Functions ---

float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
