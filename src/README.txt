Reference code 

/**
 * ESP32 <-> nRF24L01 Real-Symmetric Discovery + Ping (VSPI/HSPI)
 * - No fixed roles; both nodes run the same code.
 * - Node name = N-XXXXXX (from MAC). Unicast addr derived from name.
 * - Broadcast HELLO on pipe0 (ACK OFF). Unicast PING/PONG on pipe1 (ACK ON).
 * - Symmetry breaker: lexicographically smaller name initiates pings.
 *
 * Pins: CE=17, CSN=5, SCK=18, MISO=19, MOSI=23
 * Bus:  USE_VSPI selects VSPI(1)/HSPI(0) — same pins work via GPIO matrix
 *
 * Library: RF24 by TMRh20
 */

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <algorithm>

/* ===================== user config ===================== */

#define USE_VSPI               1        // 1: VSPI, 0: HSPI
#define RADIO_DIAG             1

#define PIN_NRF_CE   17
#define PIN_NRF_CSN   5
#define PIN_NRF_SCK  18
#define PIN_NRF_MISO 19
#define PIN_NRF_MOSI 23

#define RF_CHANNEL       76
#define RF_DATA_RATE     RF24_250KBPS
#define RF_PA_LEVEL      RF24_PA_LOW
#define RETRY_DELAY      5
#define RETRY_COUNT      5

// Full 5-byte broadcast address for pipe0 (OK because pipe0 has full addr)
static const uint8_t BROADCAST_ADDR[5] = { 'B','C','A','S','T' };

#define HELLO_PERIOD_MS   1000
#define HELLO_JITTER_MS    150
#define PING_PERIOD_MS    1000
#define REPLY_TIMEOUT_MS   150
#define PEER_TIMEOUT_MS   5000

/* ================== bus + radio instances =============== */

#if USE_VSPI
SPIClass spiBus(VSPI);
#else
SPIClass spiBus(HSPI);
#endif

RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

/* =================== identity / address ================= */

String makeNodeNameFromMAC() {
  uint8_t mac[6];
  WiFi.macAddress(mac);               // works without WiFi.begin()
  char buf[16];
  snprintf(buf, sizeof(buf), "N-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

// Make 5-byte RF24 address from name (FNV-1a)
void makeAddrFromName(const String &name, uint8_t out[5]) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < name.length(); ++i) {
    h ^= (uint8_t)name[i];
    h *= 16777619u;
  }
  out[0] = 'U';
  out[1] = (h >>  0) & 0xFF;
  out[2] = (h >>  8) & 0xFF;
  out[3] = (h >> 16) & 0xFF;
  out[4] = (h >> 24) & 0xFF;
}

/* ===================== payloads ========================= */

enum MsgType : uint8_t { HELLO = 1, PING = 2, PONG = 3 };

struct __attribute__((packed)) HelloPkt {
  uint8_t  type;           // HELLO
  char     name[12];       // <=11 + '\0'
  uint8_t  unicast[5];
};

struct __attribute__((packed)) PingPkt {
  uint8_t  type;           // PING
  uint32_t seq;
  uint32_t t0;
  char     from[12];
  char     to[12];
};

struct __attribute__((packed)) PongPkt {
  uint8_t  type;           // PONG
  uint32_t seq;
  uint32_t t0;             // from ping
  uint32_t t1;             // time at responder
  char     from[12];
  char     to[12];
};

/* ================== peer directory (single) ============= */

struct Peer {
  bool     valid = false;
  String   name;
  uint8_t  addr[5]{};
  uint32_t lastSeenMs = 0;
};

Peer     peer;
String   myName;
uint8_t  myAddr[5]{};
uint32_t seq = 0;

bool shouldInitiateTo(const String& peerName) { return myName < peerName; }

/* ====================== radio setup ===================== */

static void rfConfig() {
  radio.setChannel(RF_CHANNEL);
  radio.setDataRate(RF_DATA_RATE);
  radio.setPALevel(RF_PA_LEVEL);
  radio.setRetries(RETRY_DELAY, RETRY_COUNT);
  radio.enableDynamicPayloads();
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);              // default ACK ON; we’ll disable it per-pipe for broadcast
}

static bool setupRadio() {
  // Initialize SPI bus with proper pins
  spiBus.begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
  
  // Give SPI bus time to stabilize
  delay(10);

  // Initialize radio with SPI bus
  if (!radio.begin(&spiBus)) {
    Serial.println(F("[ERR] radio.begin() failed (wiring/power?)"));
    return false;
  }

  // Give radio time to initialize
  delay(10);

  // Check if chip is connected before configuration
  if (!radio.isChipConnected()) {
    Serial.println(F("[ERR] nRF24 chip not detected - check wiring"));
    return false;
  }

  rfConfig();

  // Flush any existing data
  radio.flush_rx();
  radio.flush_tx();

  // Listen on:
  //  - pipe0: broadcast address (ACK OFF, full 5-byte allowed)
  //  - pipe1: own unicast address (ACK ON)
  radio.openReadingPipe(0, BROADCAST_ADDR);
  radio.setAutoAck(0, false);          // broadcast pipe has no ACK

  radio.openReadingPipe(1, myAddr);    // full 5-byte unicast
  // (ACK stays ON for pipe1)

#if RADIO_DIAG
  Serial.println(F("[OK] nRF24 present and configured"));
  radio.printDetails();
#endif

  radio.startListening();
  return true;
}

/* ================== send helpers ======================== */

// Broadcast HELLO on pipe0; set multicast=true so TX doesn't wait for ACK.
void sendBroadcastHello() {
  HelloPkt pkt{};
  pkt.type = HELLO;
  strlcpy(pkt.name, myName.c_str(), sizeof(pkt.name));
  memcpy(pkt.unicast, myAddr, 5);

  radio.stopListening();
  radio.openWritingPipe(BROADCAST_ADDR);                   // pipe0 address
  bool ok = radio.write(&pkt, sizeof(pkt), /*multicast=*/true);
  radio.startListening();

#if RADIO_DIAG
  Serial.printf("[BCAST] HELLO %s : %s\n", myName.c_str(), ok ? "SENT" : "FAIL");
#endif
}

bool sendPingToPeer() {
  if (!peer.valid) return false;

  PingPkt p{};
  p.type = PING;
  p.seq  = ++seq;
  p.t0   = millis();
  strlcpy(p.from, myName.c_str(), sizeof(p.from));
  strlcpy(p.to,   peer.name.c_str(), sizeof(p.to));

  radio.stopListening();
  radio.openWritingPipe(peer.addr);                         // unicast (ACK ON)
  bool ok = radio.write(&p, sizeof(p));
  radio.startListening();

  Serial.printf("[TX] PING #%lu -> %s : %s\n",
                (unsigned long)p.seq, peer.name.c_str(), ok ? "OK" : "FAIL");
  return ok;
}

void sendPongReply(const PingPkt& in) {
  PongPkt r{};
  r.type = PONG;
  r.seq  = in.seq;
  r.t0   = in.t0;
  r.t1   = millis();
  strlcpy(r.from, myName.c_str(), sizeof(r.from));
  strlcpy(r.to,   in.from,        sizeof(r.to));

  uint8_t dest[5]{};
  if (peer.valid && peer.name == String(in.from)) {
    memcpy(dest, peer.addr, 5);
  } else {
    makeAddrFromName(String(in.from), dest);
  }

  radio.stopListening();
  radio.openWritingPipe(dest);                               // unicast (ACK ON)
  bool ok = radio.write(&r, sizeof(r));
  radio.startListening();

  Serial.printf("  -> PONG #%lu to %s : %s\n",
                (unsigned long)r.seq, in.from, ok ? "OK" : "FAIL");
}

/* ================== receive handlers ==================== */

void handleHello(const HelloPkt& h) {
  if (String(h.name) == myName) return;     // ignore our own

  if (!peer.valid || String(h.name) < peer.name) {
    peer.valid = true;
    peer.name  = String(h.name);
    memcpy(peer.addr, h.unicast, 5);
  }
  peer.lastSeenMs = millis();

#if RADIO_DIAG
  Serial.printf("[RX] HELLO from %-11s (addr=%02X%02X%02X%02X%02X)\n",
                h.name, peer.addr[0], peer.addr[1], peer.addr[2], peer.addr[3], peer.addr[4]);
#endif
}

void handlePing(const PingPkt& p) {
  Serial.printf("[RX] PING #%lu from %s (age=%lums)\n",
                (unsigned long)p.seq, p.from, (unsigned long)(millis() - p.t0));

  if (!peer.valid || peer.name != String(p.from)) {
    peer.valid = true;
    peer.name  = String(p.from);
    makeAddrFromName(peer.name, peer.addr);
  }
  peer.lastSeenMs = millis();

  sendPongReply(p);
}

void handlePong(const PongPkt& p) {
  uint32_t rtt = millis() - p.t0;
  Serial.printf("  <- PONG #%lu from %s RTT=%lums\n",
                (unsigned long)p.seq, p.from, (unsigned long)rtt);
}

/* ============ RX loop: read-full-frame then dispatch ===== */

void serviceRadio() {
  while (radio.available()) {
    uint8_t len = radio.getDynamicPayloadSize();
    if (len == 0 || len > 32) {
      uint8_t dump[32];
      if (len == 0 || len > 32) len = 32;
      radio.read(dump, len);
      continue;
    }
    uint8_t buf[32]{};
    radio.read(buf, len);

    switch (buf[0]) {
      case HELLO: {
        HelloPkt h{};
        memcpy(&h, buf, std::min<size_t>(len, sizeof(h)));
        handleHello(h);
        break;
      }
      case PING: {
        PingPkt p{};
        memcpy(&p, buf, std::min<size_t>(len, sizeof(p)));
        handlePing(p);
        break;
      }
      case PONG: {
        PongPkt q{};
        memcpy(&q, buf, std::min<size_t>(len, sizeof(q)));
        handlePong(q);
        break;
      }
      default:
        break;
    }
  }
}

/* ===================== Arduino hooks ==================== */

uint32_t lastHelloMs = 0;
uint32_t lastPingMs  = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);  // Increased delay for serial stability

  Serial.println();
  Serial.println("=== ESP32 NRF24L01 Remote Control ===");

  myName = makeNodeNameFromMAC();
  makeAddrFromName(myName, myAddr);

  Serial.printf("Node: %s | SPI: %s | CE=%d CSN=%d SCK=%d MISO=%d MOSI=%d\n",
                myName.c_str(), USE_VSPI ? "VSPI" : "HSPI",
                PIN_NRF_CE, PIN_NRF_CSN, PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI);

  Serial.printf("Unicast addr: %02X%02X%02X%02X%02X\n",
                myAddr[0], myAddr[1], myAddr[2], myAddr[3], myAddr[4]);

  Serial.println("Initializing NRF24L01...");
  if (!setupRadio()) {
    Serial.println("[FATAL] Radio setup failed - halting");
    while (true) delay(1000);
  }

  Serial.printf("RF ch=%u rate=%s PA=%s (ACK: pipe1 ON / pipe0 OFF)\n",
                RF_CHANNEL,
                (RF_DATA_RATE==RF24_2MBPS?"2Mbps":RF_DATA_RATE==RF24_1MBPS?"1Mbps":"250kbps"),
                (RF_PA_LEVEL==RF24_PA_MIN?"MIN":RF_PA_LEVEL==RF24_PA_LOW?"LOW":RF_PA_LEVEL==RF24_PA_HIGH?"HIGH":"MAX"));
  
  Serial.println("Ready for communication!");
}

void loop() {
  serviceRadio();

  uint32_t now = millis();

  // Periodic HELLO broadcast (pipe0, multicast/no-ack)
  if (now - lastHelloMs >= HELLO_PERIOD_MS + (myAddr[1] % HELLO_JITTER_MS)) {
    lastHelloMs = now;
    sendBroadcastHello();
  }

  // Manage peer lifecycle + symmetric initiation policy
  if (peer.valid) {
    if (now - peer.lastSeenMs > PEER_TIMEOUT_MS) {
      Serial.println("[INFO] Peer timed out; waiting for HELLO…");
      peer.valid = false;
    } else if (shouldInitiateTo(peer.name)) {
      if (now - lastPingMs >= PING_PERIOD_MS) {
        lastPingMs = now;
        sendPingToPeer();

        // brief window to catch PONG; RX is serviced continuously anyway
        uint32_t t0 = millis();
        while (millis() - t0 < REPLY_TIMEOUT_MS) {
          serviceRadio();
          delay(1);
        }
      }
    }
  }
}
