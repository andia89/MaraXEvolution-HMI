#include "NextionTftUpdate.h"
#include <ctype.h>
#include <string.h>

namespace {
constexpr uint32_t HTTP_TIMEOUT_MS = 3000;
constexpr uint32_t NETWORK_IDLE_MS = 10000;
constexpr uint32_t ACK_TIMEOUT_MS = 5000;
constexpr uint32_t UART_IDLE_MS = 3000;
constexpr uint32_t PROBE_MS = 600;
constexpr uint32_t BOOT_TIMEOUT_MS = 60000;
constexpr uint32_t FAILURE_RECOVERY_MS = 12000;
// The original project uses 115200 at runtime and 9600 after a display restart.
// Also cover other baud rates a newly compiled TFT might select at boot.
constexpr uint32_t PROBE_BAUDS[] = {
  115200, 9600, 57600, 38400, 19200, 4800, 2400,
  230400, 250000, 256000, 512000, 921600
};
constexpr size_t PROBE_COUNT = sizeof(PROBE_BAUDS) / sizeof(PROBE_BAUDS[0]);
}

NextionTftUpdate::NextionTftUpdate(HardwareSerial &uart, const char *token,
                                 uint16_t port)
    : uart_(uart), token_(token), server_(port) {}

void NextionTftUpdate::begin(StartCheck startCheck) {
  startCheck_ = startCheck;
  const char *headers[] = {"X-Update-Token"};
  server_.collectHeaders(headers, 1);
  server_.on("/api/nextion/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/nextion/update", HTTP_POST, [this]() { handleStart(); });
  server_.onNotFound([this]() { sendError(404, "Unknown update endpoint"); });
  server_.begin();
  started_ = true;
}

bool NextionTftUpdate::authenticate() {
  if (token_ == nullptr || token_[0] == '\0') {
    sendError(503, "Configure a nonempty TFT_UPDATE_TOKEN in the firmware");
    return false;
  }
  if (server_.header("X-Update-Token") != token_) {
    sendError(401, "Wrong X-Update-Token");
    return false;
  }
  return true;
}

void NextionTftUpdate::sendError(int code, const char *reason) {
  JsonDocument doc;
  doc["error"] = reason;
  String body;
  serializeJson(doc, body);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

bool NextionTftUpdate::validJobId(const String &id) {
  if (id.length() != 32) return false;
  for (size_t i = 0; i < id.length(); ++i)
    if (!isxdigit(static_cast<unsigned char>(id[i]))) return false;
  return true;
}

bool NextionTftUpdate::validUrl(const String &url) {
  // Numeric IPv4 avoids an unbounded external DNS lookup during an update.
  // The supplied Python server produces precisely this kind of URL.
  if (!url.startsWith("http://") || url.length() > 512) return false;
  for (size_t i = 0; i < url.length(); ++i)
    if (static_cast<unsigned char>(url[i]) <= 32 || url[i] == '#') return false;
  int slash = url.indexOf('/', 7);
  if (slash < 0) return false;
  String authority = url.substring(7, slash);
  int colon = authority.indexOf(':');
  String host = colon < 0 ? authority : authority.substring(0, colon);
  IPAddress ip;
  if (!ip.fromString(host)) return false;
  if (colon >= 0) {
    String port = authority.substring(colon + 1);
    if (port.length() == 0 || port.length() > 5) return false;
    for (size_t i = 0; i < port.length(); ++i)
      if (!isdigit(static_cast<unsigned char>(port[i]))) return false;
    if (port.toInt() < 1 || port.toInt() > 65535) return false;
  }
  return true;
}

void NextionTftUpdate::handleStart() {
  if (!authenticate()) return;
  if (!server_.hasArg("plain") || server_.arg("plain").length() > 1024) {
    sendError(400, "Expected a JSON body of at most 1024 bytes");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    sendError(400, "Invalid JSON");
    return;
  }
  String id = doc["job_id"] | "";
  String url = doc["url"] | "";
  if (!validJobId(id) || !validUrl(url) || !doc["size"].is<uint32_t>() ||
      !doc["timeout_s"].is<uint32_t>()) {
    sendError(400, "Require job_id (32 hex), numeric IPv4 HTTP URL, size and timeout_s");
    return;
  }
  uint32_t size = doc["size"].as<uint32_t>();
  uint32_t timeout = doc["timeout_s"].as<uint32_t>();
  if (size == 0 || size > MAX_FILE_SIZE || timeout < 30 ||
      timeout > MAX_TIMEOUT_SECONDS) {
    sendError(400, "Invalid size (1..128 MiB) or timeout_s (30..21600)");
    return;
  }
  // A retransmitted POST with the same identity NEVER flashes a second time.
  if (jobId_.length() && id == jobId_) {
    if (url != url_ || size != fileSize_ || timeout * 1000UL != timeoutMs_)
      sendError(409, "This job_id was already used for a different request");
    else
      sendStatus(busy() ? 202 : 200);
    return;
  }
  if (busy()) {
    sendError(409, "Another display update is in progress");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Wi-Fi is not connected");
    return;
  }
  if (startCheck_) {
    const char *reason = startCheck_();
    if (reason) {
      sendError(409, reason);
      return;
    }
  }
  jobId_ = id;
  url_ = url;
  fileSize_ = size;
  timeoutMs_ = timeout * 1000UL;
  acknowledged_ = sent_ = 0;
  blockSize_ = blockUsed_ = blockSent_ = 0;
  finished_ = uploadCommandSent_ = transferComplete_ = false;
  pendingResult_ = "";
  status_ = "running";
  message_ = "Update accepted";
  startedAt_ = phaseAt_ = millis();
  phase_ = Phase::Queued;
  sendStatus(202); // Respond first; do not run the upload inside this HTTP handler.
}

void NextionTftUpdate::handleStatus() {
  if (authenticate()) sendStatus(200);
}

void NextionTftUpdate::sendStatus(int code) {
  JsonDocument doc;
  doc["job_id"] = jobId_;
  doc["status"] = status_;
  doc["phase"] = phaseName();
  doc["message"] = message_;
  doc["busy"] = busy();
  doc["size"] = fileSize_;
  doc["sent"] = sent_;
  doc["acknowledged"] = acknowledged_;
  doc["display_ready"] = displayReady_;
  doc["baud"] = NORMAL_BAUD;
  doc["timeout_s"] = timeoutMs_ / 1000;
  String body;
  serializeJson(doc, body);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

bool NextionTftUpdate::busy() const { return phase_ != Phase::Idle; }

bool NextionTftUpdate::takeFinished(bool &ready) {
  if (!finished_) return false;
  finished_ = false;
  ready = displayReady_;
  return true;
}

bool NextionTftUpdate::recovering() const {
  return phase_ == Phase::RecoverDelay || phase_ == Phase::RecoverProbe ||
         phase_ == Phase::NormalizeDelay || phase_ == Phase::NormalizeWait;
}

const char *NextionTftUpdate::phaseName() const {
  switch (phase_) {
    case Phase::Idle: return "idle";
    case Phase::Queued: return "queued";
    case Phase::OpenHttp: return "connecting_http";
    case Phase::ReadBlock: return "reading_file";
    case Phase::Probe: return "finding_display";
    case Phase::WaitStart: return "waiting_for_upload_ready";
    case Phase::SendBlock: return "sending";
    case Phase::WaitAck: return "waiting_for_ack";
    default: return "reconnecting_display";
  }
}

void NextionTftUpdate::handle() {
  if (!started_) return;
  server_.handleClient();
  if (busy()) tick();
}

void NextionTftUpdate::tick() {
  const uint32_t now = millis();
  if (!recovering() && now - startedAt_ >= timeoutMs_) {
    beginRecovery("timeout", "Overall upload deadline exceeded");
    return;
  }
  if (recovering() && now - recoveryAt_ >= recoveryLimitMs_) {
    if (transferComplete_) {
      pendingResult_ = "timeout";
      message_ = "All bytes acknowledged, but the display did not return to its application";
    }
    finish(false);
    return;
  }
  switch (phase_) {
    case Phase::Queued:
      // Give the HTTP acknowledgement time to reach the client.
      if (now - phaseAt_ >= 100) phase_ = Phase::OpenHttp;
      break;
    case Phase::OpenHttp:
      openHttp();
      break;
    case Phase::ReadBlock:
      readBlock();
      break;
    case Phase::Probe:
      if (readFrame() && frameSize_ >= 6 && strncmp(frame_, "comok ", 6) == 0) {
        // This project uses standard protocol with address 0, as does NextionX2.
        const char *firstComma = strchr(frame_, ',');
        const char *secondComma = firstComma ? strchr(firstComma + 1, ',') : nullptr;
        const char *hyphen = firstComma ? strchr(firstComma + 1, '-') : nullptr;
        if (hyphen && secondComma && hyphen < secondComma && atoi(hyphen + 1) != 0)
          beginRecovery("error", "Nextion address mode is unsupported; use address 0");
        else
          beginUpload();
      } else if (now - phaseAt_ >= PROBE_MS) {
        if (++probeIndex_ >= PROBE_COUNT)
          beginRecovery("timeout", "Nextion did not answer connect at any supported baud rate");
        else
          startProbe(false);
      }
      break;
    case Phase::WaitStart:
      if (readAck()) {
        phase_ = Phase::SendBlock;
        lastByteAt_ = now;
      } else if (now - phaseAt_ >= ACK_TIMEOUT_MS) {
        beginRecovery("timeout", "Nextion did not acknowledge the upload command");
      }
      break;
    case Phase::SendBlock: {
      int space = uart_.availableForWrite();
      if (space > 0) {
        size_t n = blockSize_ - blockSent_;
        if (n > static_cast<size_t>(space)) n = static_cast<size_t>(space);
        if (n > 256) n = 256;
        size_t written = uart_.write(block_ + blockSent_, n);
        if (written > 0) {
          blockSent_ += written;
          sent_ += written;
          lastByteAt_ = now;
        }
      }
      if (blockSent_ == blockSize_) {
        uart_.flush(true); // TX only: keep the early 0x05 ACK in the RX buffer.
        phase_ = Phase::WaitAck;
        phaseAt_ = millis();
      } else if (now - lastByteAt_ >= UART_IDLE_MS) {
        beginRecovery("timeout", "UART transmit stalled");
      }
      break;
    }
    case Phase::WaitAck:
      if (readAck()) {
        acknowledged_ += blockSize_;
        if (acknowledged_ == fileSize_) {
          transferComplete_ = true;
          beginRecovery("success", "All bytes acknowledged; waiting for display application");
        } else {
          blockSize_ = fileSize_ - acknowledged_;
          if (blockSize_ > sizeof(block_)) blockSize_ = sizeof(block_);
          blockUsed_ = blockSent_ = 0;
          lastByteAt_ = now;
          phase_ = Phase::ReadBlock;
        }
      } else if (now - phaseAt_ >= ACK_TIMEOUT_MS) {
        beginRecovery("timeout", "Nextion block acknowledgement timed out");
      }
      break;
    case Phase::RecoverDelay:
      // A completed upload can trigger internal display firmware updates.
      // Do not send a reset command; let the display finish its own restart.
      if (now - phaseAt_ >= 2000) {
        probeIndex_ = 0;
        startProbe(true);
      }
      break;
    case Phase::RecoverProbe:
      if (readFrame() && frameSize_ == 2 && static_cast<uint8_t>(frame_[0]) == 0x66) {
        command("baud=115200");
        changeBaud(NORMAL_BAUD);
        phase_ = Phase::NormalizeDelay;
        phaseAt_ = millis();
      } else if (now - phaseAt_ >= PROBE_MS) {
        probeIndex_ = (probeIndex_ + 1) % PROBE_COUNT;
        startProbe(true);
      }
      break;
    case Phase::NormalizeDelay:
      if (now - phaseAt_ >= 60) {
        drainRx();
        command("sendme");
        phase_ = Phase::NormalizeWait;
        phaseAt_ = millis();
      }
      break;
    case Phase::NormalizeWait:
      if (readFrame() && frameSize_ == 2 && static_cast<uint8_t>(frame_[0]) == 0x66) {
        finish(true);
      } else if (now - phaseAt_ >= PROBE_MS) {
        probeIndex_ = (probeIndex_ + 1) % PROBE_COUNT;
        startProbe(true);
      }
      break;
    default: break;
  }
}

void NextionTftUpdate::openHttp() {
  if (WiFi.status() != WL_CONNECTED) {
    beginRecovery("error", "Wi-Fi disconnected before download");
    return;
  }
  http_.setConnectTimeout(HTTP_TIMEOUT_MS);
  http_.setTimeout(HTTP_TIMEOUT_MS);
  http_.setReuse(false);
  http_.useHTTP10(true);
  if (!http_.begin(network_, url_)) {
    beginRecovery("error", "Could not open TFT URL");
    return;
  }
  const char *headers[] = {"Transfer-Encoding", "Content-Encoding"};
  http_.collectHeaders(headers, 2);
  int code = http_.GET();
  if (code != HTTP_CODE_OK) {
    beginRecovery("error", String("TFT download HTTP result: ") + String(code));
    return;
  }
  String encoding = http_.header("Content-Encoding");
  if (http_.getSize() != static_cast<int>(fileSize_) ||
      http_.header("Transfer-Encoding").length() ||
      (encoding.length() && encoding != "identity")) {
    beginRecovery("error", "TFT requires an exact Content-Length and an unencoded response");
    return;
  }
  blockSize_ = fileSize_ < sizeof(block_) ? fileSize_ : sizeof(block_);
  blockUsed_ = blockSent_ = 0;
  lastByteAt_ = millis();
  phase_ = Phase::ReadBlock;
  message_ = "Downloading TFT in 4096-byte blocks";
}

void NextionTftUpdate::readBlock() {
  auto *stream = http_.getStreamPtr();
  if (stream == nullptr) {
    beginRecovery("error", "TFT HTTP stream is unavailable");
    return;
  }
  int available = stream->available();
  if (available > 0) {
    size_t n = blockSize_ - blockUsed_;
    if (n > static_cast<size_t>(available)) n = static_cast<size_t>(available);
    int count = stream->read(block_ + blockUsed_, n);
    if (count > 0) {
      blockUsed_ += static_cast<size_t>(count);
      lastByteAt_ = millis();
    }
  }
  if (blockUsed_ == blockSize_) {
    if (!uploadCommandSent_) {
      probeIndex_ = 0;
      startProbe(false);
    } else {
      phase_ = Phase::SendBlock;
      lastByteAt_ = millis();
    }
  } else if (WiFi.status() != WL_CONNECTED ||
             (!stream->connected() && stream->available() == 0)) {
    beginRecovery("error", "TFT connection closed before the declared file size arrived");
  } else if (millis() - lastByteAt_ >= NETWORK_IDLE_MS) {
    beginRecovery("timeout", "No TFT data received for 10 seconds");
  }
}

void NextionTftUpdate::drainRx() {
  // A noisy UART must not create an infinite drain loop.
  for (int i = 0; i < 512 && uart_.available() > 0; ++i) uart_.read();
  frameSize_ = 0;
  terminators_ = 0;
}

void NextionTftUpdate::changeBaud(uint32_t baud) {
  uart_.flush(true);
  uart_.updateBaudRate(baud); // Preserve the user's D9/D8 pin mapping.
  activeBaud_ = baud;
}

void NextionTftUpdate::command(const char *text) {
  static const uint8_t end[] = {0xFF, 0xFF, 0xFF};
  uart_.write(reinterpret_cast<const uint8_t *>(text), strlen(text));
  uart_.write(end, sizeof(end));
  uart_.flush(true);
}

void NextionTftUpdate::startProbe(bool recoveringDisplay) {
  changeBaud(PROBE_BAUDS[probeIndex_]);
  drainRx();
  command(""); // Terminate a partial instruction, if any.
  command(recoveringDisplay ? "sendme" : "connect");
  phase_ = recoveringDisplay ? Phase::RecoverProbe : Phase::Probe;
  phaseAt_ = millis();
}

bool NextionTftUpdate::readFrame() {
  // readFrame() returns ONE complete Nextion frame, excluding FF FF FF.
  // A fixed per-call budget keeps even a continuously noisy UART cooperative.
  for (int budget = 512; budget > 0 && uart_.available() > 0; --budget) {
    int c = uart_.read();
    if (c < 0) break;
    if (terminators_ == 3) {
      frameSize_ = 0;
      terminators_ = 0;
    }
    if (frameSize_ < sizeof(frame_) - 1) frame_[frameSize_++] = static_cast<char>(c);
    else { frameSize_ = 0; terminators_ = 0; }
    terminators_ = c == 0xFF ? terminators_ + 1 : 0;
    if (terminators_ == 3) {
      frameSize_ = frameSize_ >= 3 ? frameSize_ - 3 : 0;
      frame_[frameSize_] = '\0';
      return true;
    }
  }
  return false;
}

bool NextionTftUpdate::readAck() {
  for (int budget = 512; budget > 0 && uart_.available() > 0; --budget) {
    if (uart_.read() == 0x05) return true;
  }
  return false;
}

void NextionTftUpdate::beginUpload() {
  drainRx();
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "whmi-wri %lu,%lu,0",
           static_cast<unsigned long>(fileSize_), static_cast<unsigned long>(NORMAL_BAUD));
  command(cmd);
  // Treat even a lost initial ACK as a possibly interrupted flash.
  uploadCommandSent_ = true;
  displayReady_ = false;
  changeBaud(NORMAL_BAUD);
  phase_ = Phase::WaitStart;
  phaseAt_ = millis();
  message_ = "Transferring TFT to Nextion";
}

void NextionTftUpdate::beginRecovery(const char *result, const String &reason) {
  http_.end();
  network_.stop();
  pendingResult_ = result;
  message_ = reason;
  // Before probing/sending to the display, a download failure leaves its
  // application alone. Restore the host UART and resume immediately.
  if (!uploadCommandSent_ && (phase_ == Phase::Queued || phase_ == Phase::OpenHttp ||
                             (phase_ == Phase::ReadBlock && sent_ == 0))) {
    finish(displayReady_);
    return;
  }
  recoveryAt_ = phaseAt_ = millis();
  recoveryLimitMs_ = transferComplete_ ? BOOT_TIMEOUT_MS : FAILURE_RECOVERY_MS;
  phase_ = Phase::RecoverDelay;
}

void NextionTftUpdate::finish(bool ready) {
  http_.end();
  network_.stop();
  changeBaud(NORMAL_BAUD);
  drainRx();
  displayReady_ = ready;
  status_ = pendingResult_;
  if (status_ == "success") message_ = "TFT acknowledged and display application responding at 115200 baud";
  if (!ready) message_ += "; ESP32 resumed; display may need power cycling and a fresh TFT upload";
  phase_ = Phase::Idle;
  finished_ = true;
}
