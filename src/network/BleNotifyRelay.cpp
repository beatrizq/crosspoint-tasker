#include "BleNotifyRelay.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <NimBLEDevice.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BleNotificationQueue.h"

namespace {

// Nordic UART Service: one write-in characteristic (phone -> device), one
// notify-out characteristic (device -> phone). Verified against
// Gadgetbridge's own BangleJSConstants.java (UUID_SERVICE_NORDIC_UART /
// UUID_CHARACTERISTIC_NORDIC_UART_TX / _RX -- named from the phone's own
// perspective there, which is why its "TX" is this device's write-in side).
// Unauthenticated by design: NUS carries no pairing requirement of its own,
// and this code never calls NimBLEDevice::setSecurityAuth(), so no passkey
// dialog or bonding is ever requested from either side.
constexpr char SERVICE_UUID[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char CHAR_WRITE_UUID[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char CHAR_NOTIFY_UUID[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
// Must start with "Bangle.js" to match Gadgetbridge's BangleJSCoordinator
// device-name regex ("Bangle\.js.*") for its Bangle.js device support to
// offer pairing at all. The rest is the board's own model identifier
// (BoardConfig::ACTIVE.name, e.g. "xteink_x3") rather than a fixed literal,
// so the advertised name always matches whatever hardware actually detected
// itself -- no per-model string to keep in sync by hand. Built once, lazily,
// into a static buffer: BoardConfig::ACTIVE is a runtime value (X3 vs X4 on
// the shared C3 binary is resolved by setupDisplayAndFonts()'s detection,
// which always runs before the first bringUp()/resume() call from
// BleNotifyRelay::begin()), so it can't be a constexpr.
const char* deviceName() {
  static char nameBuf[32];
  if (nameBuf[0] == '\0') {
    snprintf(nameBuf, sizeof(nameBuf), "Bangle.js %s", BoardConfig::ACTIVE.name);
  }
  return nameBuf;
}

// Held for the lifetime of a connection: without it, HalPowerManager throttles
// the CPU to LOW_POWER_FREQ (10 MHz, no-PSRAM board) after 3s of button
// idleness, which starves the NimBLE host task and drops the link unless the
// user keeps touching the device. Single connection by design (see
// CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1 in platformio.ini), so one Lock at a
// time is exactly what HalPowerManager::Lock supports.
std::unique_ptr<HalPowerManager::Lock> connectionLock;

// Held for as long as NimBLE is merely advertising, i.e. from a successful
// bringUp()/resume() until pause(). Without this, the CPU throttles to
// LOW_POWER_FREQ 3s after the last button press even while advertising (not
// just while connected) -- starving the NimBLE host task and making it too
// slow to complete Android's connection handshake most of the time, which is
// the leading suspect for unreliable Gadgetbridge reconnects. This and
// connectionLock are separate, both-refcounted HalPowerManager::Lock
// instances (Lock supports multiple concurrent holders) rather than one
// shared one, so each can be released independently of the other's state:
// advertisingLock always covers "NimBLE is up at all", connectionLock
// narrows further to "and a peer is attached" for onDisconnect() to manage on
// its own.
//
// This effectively holds the CPU at full speed for the device's entire awake
// lifetime, since nothing pauses BLE before the real sleep trigger
// (main.cpp's enterDeepSleep() calls activityManager.goToSleep() and tears
// down WiFi/tilt-sensor/display, but never BleNotifyRelay::pause(), before
// powerManager.startDeepSleep() cuts power outright) -- i.e. exactly the
// user's configured "time to sleep" window, not an unbounded condition.
// Battery-life tradeoff: this is the intentional cost of a device that stays
// reliably reachable over BLE for its whole awake period, matched to a
// setting the user already controls.
std::unique_ptr<HalPowerManager::Lock> advertisingLock;

// Tracks whether NimBLE is currently brought up, vs. torn down for pause().
// poll() and resume() use this to no-op/rebuild correctly.
bool active = false;

// The device->phone side of the Nordic UART Service, set once in bringUp()
// (called only from begin()). Survives pause()/resume() untouched, same as
// the server/service objects it belongs to (see resume()'s own doc comment),
// so this pointer stays valid for the object's whole lifetime once set --
// resume() need not (and does not) reassign it.
NimBLECharacteristic* notifyCharacteristic = nullptr;

// bringUp()'s real, dynamic heap cost (NimBLE's heap_caps_malloc-based mbuf
// pools/buffers, connection state, host task stack -- not the static
// .bss/.data a binary-size analysis alone would catch). Folded into poll()'s
// periodic status log below rather than logged once in bringUp() itself,
// since this board's USB re-enumerates on every reset (see this file's own
// header comment) -- a one-shot line at boot is easy to miss before a serial
// terminal reconnects.
int32_t bleInitHeapCost = 0;

// Set for the duration of pause()'s NimBLEDevice::deinit() call. deinit()
// disconnects any connected peer as part of its own teardown, which fires
// onDisconnect() below from *inside* that teardown -- a real crash
// (heap_caps_free assert, "free() target pointer is outside heap areas") came
// from onDisconnect() reacting to that by calling
// NimBLEDevice::startAdvertising(), reentering NimBLE APIs mid-teardown. This
// flag tells onDisconnect() to stay passive when the disconnect is pause()'s
// own doing.
bool pausing = false;

// Reassembly buffer for one GB({...}) command. Gadgetbridge chunks a command
// across multiple ~20-byte BLE writes with no per-chunk framing (BangleJS
// DeviceSupport.java's uartTx()) and terminates it with a single '\n' -- the
// device is expected to buffer until that terminator, not treat each write as
// a complete message.
//
// Sized for the realistic common case, not the pathological worst case:
// Gadgetbridge caps title/subject/sender/body at 80/80/40/400 original
// characters (cropToLength calls in onNotification()), each individually
// hex-escaped as "\xHH" per UTF-8 byte for anything outside plain ASCII (see
// jsonToStringInternal) -- a message that is entirely 2-byte-UTF-8 accented
// text expands to roughly (80+80+40+400)*2 bytes*4 chars/escape =~ 4.8KB
// including JSON overhead. A message saturated with 3-4 byte UTF-8 (dense
// CJK/emoji) could in theory run past that; such a message is simply dropped
// (see the overflow check in onWrite() below) rather than sized for -- a
// permanently-reserved worst-case buffer for a rare edge case is a poor
// trade on this board's RAM budget, and a dropped notification is a graceful
// degradation CLAUDE.md's error-handling philosophy already favors over
// crashing or truncating into invalid JSON.
constexpr size_t CMD_BUFFER_SIZE = 4096;
char cmdBuffer[CMD_BUFFER_SIZE];
size_t cmdLen = 0;
bool cmdOverflowed = false;

// Converts Gadgetbridge's JS-literal string escapes -- "\xHH" (hex byte, used
// for UTF-8 continuation/lead bytes and other bytes standard JSON can't
// express directly), "\v" (vertical tab, not a JSON escape at all), and its
// octal-style forms for other control bytes (e.g. "\20" for byte 16, DLE) --
// into plain bytes ArduinoJson's deserializeJson() can parse. Standard JSON
// escapes ("\" \\ \/ \b \f \n \r \t \uXXXX") are left untouched for
// deserializeJson() itself to handle. Every conversion replaces a longer
// escape sequence with the single byte it represents, so this always shrinks
// or preserves length and can run in place. Returns the new length.
size_t unescapeGadgetbridgeEscapes(char* buf, const size_t len) {
  size_t readIdx = 0;
  size_t writeIdx = 0;
  while (readIdx < len) {
    const char c = buf[readIdx];
    if (c != '\\' || readIdx + 1 >= len) {
      buf[writeIdx++] = c;
      readIdx++;
      continue;
    }
    const char next = buf[readIdx + 1];
    if (next == 'x' && readIdx + 3 < len) {
      const char hex[3] = {buf[readIdx + 2], buf[readIdx + 3], '\0'};
      buf[writeIdx++] = static_cast<char>(strtol(hex, nullptr, 16));
      readIdx += 4;
    } else if (next == 'v') {
      // Not a JSON escape and not meaningful to render -- drop it.
      readIdx += 2;
    } else if (next >= '0' && next <= '7') {
      // Gadgetbridge's octal form, e.g. "\20" for byte 16 (DLE). Consume up
      // to 2 octal digits, matching the longest form it ever emits.
      int value = next - '0';
      size_t digits = 1;
      if (readIdx + 2 < len && buf[readIdx + 2] >= '0' && buf[readIdx + 2] <= '7') {
        value = value * 8 + (buf[readIdx + 2] - '0');
        digits = 2;
      }
      buf[writeIdx++] = static_cast<char>(value);
      readIdx += 1 + digits;
    } else {
      // A standard JSON escape ('"' '\\' '/' 'b' 'f' 'n' 'r' 't') or the start
      // of \uHHHH -- leave both characters for deserializeJson() to handle.
      buf[writeIdx++] = c;
      buf[writeIdx++] = next;
      readIdx += 2;
    }
  }
  return writeIdx;
}

// Defined further down with the other device->phone senders.
void sendNotifyAck(uint32_t id);

// Handles one fully-reassembled, unescaped command. Anything other than a
// GB({...}) JSON call is some other Espruino snippet Gadgetbridge also sends
// (setTime(...), storage writes, ...) -- nothing here to parse, and nothing
// that needs a reply either way.
void processCommand(char* buf, size_t len) {
  if (len < 4 || buf[0] != 'G' || buf[1] != 'B' || buf[2] != '(' || buf[len - 1] != ')') {
    LOG_DBG("BLE", "Command (not GB JSON): %.*s", static_cast<int>(len), buf);
    return;
  }

  char* json = buf + 3;
  const size_t rawJsonLen = len - 4;  // strip leading "GB(" and trailing ")"
  const size_t jsonLen = unescapeGadgetbridgeEscapes(json, rawJsonLen);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, jsonLen);
  if (err) {
    LOG_DBG("BLE", "GB() JSON parse failed: %s", err.c_str());
    return;
  }

  const char* type = doc["t"] | "";
  uint8_t hour = 0;
  uint8_t minute = 0;

  if (strcmp(type, "notify") == 0) {
    const char* src = doc["src"] | "";
    const char* title = doc["title"] | "";
    const char* body = doc["body"] | "";
    const uint32_t id = doc["id"] | static_cast<uint32_t>(0);
    halClock.getTime(hour, minute);
    BLE_NOTIFICATIONS.push(id, false, src, title, body, hour, minute);
    BLE_NOTIFICATIONS.saveToFile();
    LOG_INF("BLE", "Notification from %s: %s: %s", src, title, body);
    // Tell the phone this one is stored, so it stops sending it again. Sent after
    // the queue is saved, and also when push() found it already held (a resend
    // of something the device has): either way the device has it.
    sendNotifyAck(id);
  } else if (strcmp(type, "call") == 0) {
    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "incoming") == 0) {
      const char* name = doc["name"] | "";
      const char* number = doc["number"] | "";
      const bool hasName = name[0] != '\0';
      halClock.getTime(hour, minute);
      // The number rides in `content` only when a name is already shown as the
      // title -- otherwise the number IS the title, and repeating it in
      // content would just duplicate the line.
      BLE_NOTIFICATIONS.push(0, true, tr(STR_BLE_INCOMING_CALL), hasName ? name : number, hasName ? number : "", hour,
                             minute);
      BLE_NOTIFICATIONS.saveToFile();
      LOG_INF("BLE", "Incoming call: %s / %s", name, number);
    }
    // Other cmd values (accept/reject/outgoing/start/end, ...) aren't shown --
    // this queue is "who tried to reach me", not a full call-state mirror.
  }
  // "notify-" (remote dismiss) and every other "t" (weather, musicinfo,
  // actfetch, is_gps_active, ...): see BleNotificationQueue's own doc comment
  // for why dismiss isn't tracked. No reply required for any of these either
  // way -- confirmed against Gadgetbridge's own BangleJSDeviceSupport.java,
  // which just logs "packet type '...' not understood" and moves on.
}

// Step 1 of the bonding follow-up (see this session's scoping notes): makes
// bonding POSSIBLE without making it REQUIRED -- the write characteristic
// below is still plain WRITE/WRITE_NR, so an unbonded Gadgetbridge (today's
// only tested configuration) keeps working exactly as before. A bond only
// happens if Gadgetbridge's own bonding UI (BangleJSCoordinator's
// BONDING_STYLE_ASK) is used to request one. Requiring it (WRITE_ENC) is a
// deliberate later step, once a bond is confirmed to actually form and
// survive reconnects on real hardware.
void configureSecurity() {
  // Just Works: no passkey/numeric-comparison UI, since this device has no
  // screen or keyboard to show or enter one. mitm=false because Just Works
  // cannot offer MITM protection by definition (no out-of-band channel to
  // authenticate against); sc=true (LE Secure Connections) is the modern
  // pairing algorithm and costs nothing extra here -- NimBLE's crypto is
  // already compiled into this binary either way (see platformio.ini's own
  // SM_LEGACY/SM_SC revert note).
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& connInfo) override {
    LOG_INF("BLE", "Connected: %s", connInfo.getAddress().toString().c_str());
    connectionLock = makeUniqueNoThrow<HalPowerManager::Lock>();
    if (!connectionLock) {
      LOG_ERR("BLE", "OOM: HalPowerManager::Lock (%u bytes)", static_cast<unsigned>(sizeof(HalPowerManager::Lock)));
    }
  }

  void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/, const int reason) override {
    connectionLock.reset();
    if (pausing) {
      LOG_INF("BLE", "Disconnected (reason %d) -- pausing, not resuming advertising", reason);
      return;
    }
    LOG_INF("BLE", "Disconnected (reason %d) -- resuming advertising", reason);
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(const uint16_t mtu, NimBLEConnInfo& /*connInfo*/) override { LOG_INF("BLE", "MTU: %u", mtu); }

  // Diagnostic only, for confirming on real hardware whether Gadgetbridge's
  // own bonding UI actually produced a bond (see configureSecurity()'s own
  // comment) -- fires whether or not a bond resulted, since Just Works still
  // completes an (unbonded) encrypted-for-this-session pairing even when the
  // peer declines to bond.
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    LOG_INF("BLE", "Auth complete: bonded=%d encrypted=%d authenticated=%d", connInfo.isBonded(),
            connInfo.isEncrypted(), connInfo.isAuthenticated());
  }
};

class WriteCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& /*connInfo*/) override {
    const auto& value = chr->getValue();
    const char* data = value.c_str();
    const size_t len = value.length();

    for (size_t i = 0; i < len; i++) {
      const char b = data[i];
      // DLE (0x10): Espruino console convention for "discard whatever is
      // currently buffered" -- Gadgetbridge itself prefixes every command
      // with this byte (uartTxJSON's "GB(...)" framing), so treating it
      // as a hard reset here also makes us robust to a prior command that
      // never reached its '\n' (e.g. a dropped BLE packet).
      if (b == '\x10') {
        cmdLen = 0;
        cmdOverflowed = false;
        continue;
      }
      if (b == '\n') {
        if (cmdOverflowed) {
          LOG_ERR("BLE", "Command exceeded %u-byte buffer, discarded", static_cast<unsigned>(CMD_BUFFER_SIZE));
        } else if (cmdLen > 0) {
          processCommand(cmdBuffer, cmdLen);
        }
        cmdLen = 0;
        cmdOverflowed = false;
        continue;
      }
      if (cmdLen >= CMD_BUFFER_SIZE) {
        cmdOverflowed = true;  // Logged once, on the '\n' above, not per byte.
        continue;
      }
      cmdBuffer[cmdLen++] = b;
    }
  }

  void onSubscribe(NimBLECharacteristic* /*chr*/, NimBLEConnInfo& /*connInfo*/, const uint16_t subValue) override {
    LOG_INF("BLE", "Subscribe state: %u", subValue);
  }
};

ServerCallbacks serverCallbacks;
WriteCallbacks writeCallbacks;

// Builds the server/service/characteristics from scratch and starts
// advertising. Called once, from begin() only -- resume() reuses these same
// objects rather than rebuilding, since pause()'s deinit(false) leaves them
// intact by design (see the deinit(bool) doc comment in NimBLEDevice.h).
void bringUp() {
  const uint32_t freeHeapBefore = ESP.getFreeHeap();

  NimBLEDevice::init(deviceName());
  configureSecurity();

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  NimBLEService* service = server->createService(SERVICE_UUID);
  NimBLECharacteristic* writeChar =
      service->createCharacteristic(CHAR_WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  writeChar->setCallbacks(&writeCallbacks);
  notifyCharacteristic = service->createCharacteristic(CHAR_NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);
  // No NimBLEService::start() call: it is a deprecated no-op in this library
  // version (2.5.x) -- the server and its services start together, below.

  // A legacy advertising packet is capped at 31 bytes; the name (now
  // model-dependent length, ~20-25 bytes) and the 128-bit service UUID
  // (18 bytes) don't both fit alongside the 3-byte flags structure NimBLE
  // always adds. Whichever doesn't fit spills
  // into the scan response automatically -- but enableScanResponse() must be
  // called first for that overflow to be decided the way we want: the
  // service UUID is what a scanner's device-detection filter actually keys
  // on, so it needs to land in the primary packet; the name can safely ride
  // in scan response, since anything doing a real pairing flow (not just a
  // passive scan) reads both packets anyway.
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->addServiceUUID(service->getUUID());
  advertising->setName(deviceName());
  if (!advertising->start()) {
    LOG_ERR("BLE", "Failed to start advertising -- Gadgetbridge will never see this device");
    return;
  }

  active = true;
  advertisingLock = makeUniqueNoThrow<HalPowerManager::Lock>();
  if (!advertisingLock) {
    LOG_ERR("BLE", "OOM: HalPowerManager::Lock (%u bytes)", static_cast<unsigned>(sizeof(HalPowerManager::Lock)));
  }
  bleInitHeapCost = static_cast<int32_t>(freeHeapBefore) - static_cast<int32_t>(ESP.getFreeHeap());
  LOG_INF("BLE", "Advertising as \"%s\" for Gadgetbridge pairing", deviceName());
}

// GATT notifications carry no ATT-level continuation protocol of their own --
// unlike a GATT long-write, the payload is hard-capped at (negotiated MTU -
// 3) bytes, and anything past that is silently dropped by the stack, not
// fragmented. This project's negotiated MTU is 23 in practice (confirmed on
// real hardware: a live capture showed every notification/write landing as
// exactly 20-byte payloads -- see platformio.ini's own
// CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU comment), so any message longer than 20
// bytes needs multiple sequential notify() calls ourselves. No chunk framing
// of our own is needed: Gadgetbridge's own RX path already reassembles
// multiple characteristic-changed packets into one line before parsing
// (BangleJSDeviceSupport.onCharacteristicChanged: `receivedLine += packetStr`
// until it finds '\n').
constexpr size_t MAX_NOTIFY_CHUNK = 20;

void notifyChunked(const char* data, const size_t len) {
  if (notifyCharacteristic == nullptr) return;
  size_t offset = 0;
  while (offset < len) {
    const size_t chunkLen = (len - offset < MAX_NOTIFY_CHUNK) ? (len - offset) : MAX_NOTIFY_CHUNK;
    if (!notifyCharacteristic->notify(reinterpret_cast<const uint8_t*>(data + offset), chunkLen)) {
      LOG_ERR("BLE", "notify() failed at chunk offset %u/%u", static_cast<unsigned>(offset),
              static_cast<unsigned>(len));
      return;
    }
    offset += chunkLen;
  }
}

// Pushes a Bangle.js-protocol "status" packet -- the same message type real
// Bangle.js firmware sends for its own battery reporting, so Gadgetbridge's
// existing, unmodified BangleJSDeviceSupport.handleBatteryStatus() parses and
// displays it on the device card with no app-side changes at all. Terminated
// with "\r\n", not just "\n": BangleJSDeviceSupport's own line-splitting
// (onCharacteristicChanged) drops the one byte immediately before every
// "\n" -- real Bangle.js firmware always sends CRLF for exactly this reason,
// so matching that convention (rather than bare "\n", which would silently
// truncate our own closing '}' and fail JSON parsing on every single update)
// is what keeps the message valid on the phone side.
void sendBatteryStatus() {
  if (notifyCharacteristic == nullptr) return;
  char buf[48];
  const int len = snprintf(buf, sizeof(buf), "{\"t\":\"status\",\"bat\":%u,\"chg\":%d}\r\n",
                           static_cast<unsigned>(powerManager.getBatteryPercentage()), gpio.isUsbConnected() ? 1 : 0);
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf)) {
    LOG_ERR("BLE", "Battery status message truncated or encoding failed");
    return;
  }
  notifyChunked(buf, static_cast<size_t>(len));
}

// The delivery receipt for a stored notification: the patched Gadgetbridge
// ("notifyAck", see BangleJSDeviceSupport) stops forwarding that notification
// once it has this, and forwards anything never acked again on the next
// reconnect -- the way a notification the device missed (asleep, out of range,
// a lost write) gets a second chance without ones it already has repeating.
// CRLF-terminated for the same reason sendBatteryStatus() is. id 0 is what a
// notification without an id parses to, and cannot be matched to anything, so
// there is nothing to confirm.
void sendNotifyAck(const uint32_t id) {
  if (notifyCharacteristic == nullptr || id == 0) return;
  char buf[48];
  const int len = snprintf(buf, sizeof(buf), "{\"t\":\"notifyAck\",\"id\":%lu}\r\n", static_cast<unsigned long>(id));
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf)) {
    LOG_ERR("BLE", "Notification ack truncated or encoding failed");
    return;
  }
  notifyChunked(buf, static_cast<size_t>(len));
}

}  // namespace

void BleNotifyRelay::begin() { bringUp(); }

void BleNotifyRelay::pause() {
  if (!active) return;
  // deinit()'s clearAll only controls whether it deletes our server/
  // advertising/service objects -- the actual heap-freeing work
  // (nimble_port_stop()/nimble_port_deinit(): host task, mbuf pools, HCI
  // buffers, connection state -- the real fix for the original OOM crash)
  // happens unconditionally either way. clearAll=true (deleting and later
  // rebuilding those objects) hit three separate internal NimBLE-Arduino
  // bugs across this project's own testing: a reentrant onDisconnect callback
  // during teardown, a central-role m_pClient deletion on an uninitialized
  // pointer, and a corrupted std::vector free in NimBLEServer's destructor.
  // clearAll=false frees the same heap without ever running any of those
  // destructors, and is also this library's own documented mechanism for
  // exactly this "pause temporarily, resume later" use case.
  pausing = true;
  connectionLock.reset();
  advertisingLock.reset();
  NimBLEDevice::deinit(false);
  pausing = false;
  active = false;
  LOG_INF("BLE", "Paused for WiFi -- disconnected any peer, freed NimBLE's heap");
}

void BleNotifyRelay::resume() {
  if (active) return;
  // Re-init only: the server/advertising/service objects created once in
  // bringUp() (called only from begin()) survived pause()'s deinit(false)
  // untouched -- NimBLEDevice::createServer()/getAdvertising() are both
  // lazy singletons that return the existing object rather than creating a
  // new one, so calling bringUp() again here would be redundant at best and
  // risk a duplicate service at worst.
  NimBLEDevice::init(deviceName());
  // deinit(false) tears down the whole host stack (see pause()'s own doc
  // comment), which resets the security config set in bringUp() -- reapply
  // it every time init() runs again, not just the first time.
  configureSecurity();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising->start()) {
    LOG_ERR("BLE", "Failed to resume advertising -- Gadgetbridge will never see this device");
    return;
  }
  active = true;
  advertisingLock = makeUniqueNoThrow<HalPowerManager::Lock>();
  if (!advertisingLock) {
    LOG_ERR("BLE", "OOM: HalPowerManager::Lock (%u bytes)", static_cast<unsigned>(sizeof(HalPowerManager::Lock)));
  }
  LOG_INF("BLE", "Resumed after WiFi operation finished");
}

void BleNotifyRelay::poll() {
  static unsigned long lastStatusLog = 0;
  const unsigned long now = millis();
  if (now - lastStatusLog < 10000) return;
  lastStatusLog = now;

  // Always logs something, even if bringUp() never got as far as setting
  // active=true -- silence here was previously ambiguous between "BLE not
  // compiled in", "bringUp() failed silently", and "succeeded but something
  // else suppressed logging", and diagnosing that live is hard on this board
  // (USB CDC re-enumerates on every reset, dropping any serial session).
  if (!active) {
    LOG_INF("BLE", "Status: NOT ACTIVE (bringUp() never completed successfully)");
    return;
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  NimBLEServer* server = NimBLEDevice::getServer();
  const bool isAdvertising = advertising != nullptr && advertising->isAdvertising();
  const unsigned connectedCount = server != nullptr ? static_cast<unsigned>(server->getConnectedCount()) : 0;
  LOG_INF("BLE", "Status: advertising=%d connected=%u, init heap cost=%d bytes", isAdvertising, connectedCount,
          bleInitHeapCost);

  // Same 10s cadence as the status log above rather than a second timer --
  // a 48-byte notify is cheap, and Gadgetbridge only ever shows the most
  // recent value anyway, so there is nothing to gain from a slower interval.
  if (connectedCount > 0) sendBatteryStatus();
}

bool BleNotifyRelay::isConnected() {
  if (!active) return false;
  // Same read poll() already does for its own status line -- getServer() is a
  // lazy singleton, non-null once bringUp() has run at least once.
  NimBLEServer* server = NimBLEDevice::getServer();
  return server != nullptr && server->getConnectedCount() > 0;
}

#else

void BleNotifyRelay::begin() {}
void BleNotifyRelay::pause() {}
void BleNotifyRelay::resume() {}
void BleNotifyRelay::poll() {}
bool BleNotifyRelay::isConnected() { return false; }

#endif
