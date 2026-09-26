#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

// Call begin() once, and handle() from loop() BEFORE any normal Nextion I/O.
// While busy(), nothing else may read or write the display UART.
// No ESP restart, filesystem partition, or full-file RAM allocation is needed.
class NextionTftUpdate {
public:
  using StartCheck = const char *(*)(); // nullptr permits starting; otherwise reason
  static constexpr uint32_t NORMAL_BAUD = 115200;
  static constexpr uint32_t MAX_FILE_SIZE = 128UL * 1024UL * 1024UL;
  static constexpr uint32_t MAX_TIMEOUT_SECONDS = 21600; // six hours

  NextionTftUpdate(HardwareSerial &uart, const char *token, uint16_t port = 8080);
  void begin(StartCheck startCheck);
  void handle();
  bool busy() const;
  bool takeFinished(bool &displayReady);
  bool displayReady() const { return displayReady_; }

private:
  enum class Phase {
    Idle, Queued, OpenHttp, ReadBlock, Probe, WaitStart, SendBlock, WaitAck,
    RecoverDelay, RecoverProbe, NormalizeDelay, NormalizeWait
  };

  HardwareSerial &uart_;
  const char *token_;
  WebServer server_;
  WiFiClient network_;
  HTTPClient http_;
  StartCheck startCheck_ = nullptr;
  bool started_ = false;
  bool finished_ = false;
  bool displayReady_ = true;
  bool uploadCommandSent_ = false;
  bool transferComplete_ = false;
  Phase phase_ = Phase::Idle;
  String jobId_, url_, status_ = "idle", message_ = "No upload requested";
  String pendingResult_;
  uint32_t fileSize_ = 0, acknowledged_ = 0, sent_ = 0;
  uint32_t startedAt_ = 0, phaseAt_ = 0, lastByteAt_ = 0;
  uint32_t timeoutMs_ = 0, recoveryAt_ = 0, recoveryLimitMs_ = 0;
  uint32_t activeBaud_ = NORMAL_BAUD;
  uint8_t probeIndex_ = 0;
  uint8_t block_[4096];
  size_t blockSize_ = 0, blockUsed_ = 0, blockSent_ = 0;
  char frame_[192];
  size_t frameSize_ = 0;
  uint8_t terminators_ = 0;

  bool authenticate();
  void handleStart();
  void handleStatus();
  void sendStatus(int code);
  void sendError(int code, const char *reason);
  void tick();
  void openHttp();
  void readBlock();
  void startProbe(bool recovering);
  bool readFrame();
  bool readAck();
  void drainRx();
  void command(const char *text);
  void changeBaud(uint32_t baud);
  void beginUpload();
  void beginRecovery(const char *result, const String &reason);
  void finish(bool ready);
  bool recovering() const;
  const char *phaseName() const;
  static bool validUrl(const String &url);
  static bool validJobId(const String &id);
};
