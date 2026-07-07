#pragma once

// PubSubClient ต้องการ WiFiClient& — WebSocketsClient ส่งข้อมูลผ่าน callback
// ต้องเรียก _ws.loop() ระหว่างรอ CONNACK ไม่งั้น available() ว่างตลอด → rc=-4
#include <WiFi.h>
#include <WebSocketsClient.h>

class MqttWsWifiClient : public WiFiClient {
 public:
  static const size_t RX_BUF_SIZE = 4096;

  explicit MqttWsWifiClient(WebSocketsClient& ws) : _ws(ws) {}

  void attachEventHandler()
  {
    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
      onWsEvent(type, payload, length);
    });
  }

  void clearRx()
  {
    _rxHead = 0;
    _rxTail = 0;
  }

  int connect(IPAddress ip, uint16_t port) override {
    (void)ip;
    (void)port;
    return _ws.isConnected() ? 1 : 0;
  }

  int connect(const char* host, uint16_t port) override {
    (void)host;
    (void)port;
    return _ws.isConnected() ? 1 : 0;
  }

#if defined(ESP32)
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    (void)host;
    (void)port;
    (void)timeout;
    return _ws.isConnected() ? 1 : 0;
  }

  int connect(IPAddress ip, uint16_t port, int32_t timeout) override {
    (void)ip;
    (void)port;
    (void)timeout;
    return _ws.isConnected() ? 1 : 0;
  }
#endif

  size_t write(uint8_t b) override {
    const bool ok = _ws.sendBIN(&b, 1);
    if (ok)
      pumpWs();
    return ok ? 1 : 0;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    const bool ok = _ws.sendBIN(const_cast<uint8_t*>(buffer), size);
    if (ok)
      pumpWs();
    return ok ? size : 0;
  }

  int available() override {
    pumpWs();
    return (int)rxAvailable();
  }

  int read() override {
    waitRx(1);
    uint8_t b = 0;
    return read(&b, 1) == 1 ? (int)b : -1;
  }

  int read(uint8_t* buf, size_t size) override {
    if (!buf || size == 0)
      return 0;
    waitRx(size);
    size_t n = 0;
    while (n < size && _rxTail != _rxHead)
    {
      buf[n++] = _rxBuf[_rxTail];
      _rxTail = (_rxTail + 1) % RX_BUF_SIZE;
    }
    return (int)n;
  }

  int peek() override {
    pumpWs();
    if (_rxTail == _rxHead)
      return -1;
    return _rxBuf[_rxTail];
  }

  void flush() override {}

  void stop() override {
    clearRx();
    _ws.disconnect();
  }

  uint8_t connected() override { return _ws.isConnected() ? 1 : 0; }

 private:
  WebSocketsClient& _ws;
  uint8_t _rxBuf[RX_BUF_SIZE];
  volatile size_t _rxHead = 0;
  volatile size_t _rxTail = 0;

  size_t rxAvailable() const
  {
    if (_rxHead >= _rxTail)
      return _rxHead - _rxTail;
    return RX_BUF_SIZE - _rxTail + _rxHead;
  }

  void pumpWs()
  {
    if (_ws.isConnected())
      _ws.loop();
  }

  void waitRx(size_t need)
  {
    if (rxAvailable() >= need)
      return;
    const unsigned long deadline = millis() + 50UL;
    while (rxAvailable() < need && _ws.isConnected() && (long)(millis() - deadline) < 0)
    {
      pumpWs();
      yield();
    }
  }

  void rxPush(uint8_t b)
  {
    size_t next = (_rxHead + 1) % RX_BUF_SIZE;
    if (next == _rxTail)
      return;
    _rxBuf[_rxHead] = b;
    _rxHead = next;
  }

  void onWsEvent(WStype_t type, uint8_t* payload, size_t length)
  {
    if (type == WStype_DISCONNECTED || type == WStype_ERROR)
    {
      clearRx();
      return;
    }
    if (type != WStype_BIN && type != WStype_TEXT)
      return;
    if (!payload || length == 0)
      return;
    for (size_t i = 0; i < length; i++)
      rxPush(payload[i]);
  }
};
