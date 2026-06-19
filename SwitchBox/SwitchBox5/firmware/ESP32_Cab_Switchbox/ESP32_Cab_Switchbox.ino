#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <Wire.h>
#include <SPI.h>
#include <Ethernet_Generic.h>
#include <EthernetUdp.h>

#define INO_DESCRIPTION "ESP32_Cab_Switchbox"

const uint16_t INO_ID = 24056;
const uint16_t EEPROM_SIZE = 512;
const uint32_t CONFIG_MAGIC = 0x53574258UL; // SWBX
const uint8_t CONFIG_VERSION = 3;
const uint8_t LEGACY_CONFIG_VERSION = 1;
const uint8_t SECTION_SWITCH_REQUIRED_CONFIG_VERSION = 2;

const uint8_t MCP23017_ADDRESS = 0x20;
const uint8_t W5500_CS_PIN = 5;
const uint8_t BUZZER_PIN = 27;
const uint16_t RATE_CONTROLLER_PORT = 29999;
const uint16_t LOCAL_UDP_PORT = 28888;
const uint16_t WEB_PORT = 80;
const uint16_t AP_DNS_PORT = 53;
const char* MDNS_NAME = "cabswitchbox";
const uint16_t BUZZER_TEST_MS = 2000;
const uint16_t ALARM_PACKET_TIMEOUT_MS = 3000;
const uint16_t BUZZER_VOLUME_FRAME_MS = 25;
const uint16_t BUZZER_REDUCED_ON_MS = 14;

enum InputAction : uint8_t
{
  ACT_DISABLED = 0,
  ACT_AUTO_MODE = 1,
  ACT_MANUAL_MODE = 2,
  ACT_OFF_MODE = 3,
  ACT_MASTER_ON = 4,
  ACT_MASTER_OFF = 5,
  ACT_RATE_UP = 6,
  ACT_RATE_DOWN = 7,
  ACT_WORK_SWITCH = 8,
  ACT_AUTO_SECTION = 9,
  ACT_AUTO_RATE = 10,
  ACT_RATE_2 = 11,
  ACT_MASTER_SWITCH = 12,
  ACT_SECTION_1 = 20,
  ACT_SECTION_2 = 21,
  ACT_SECTION_3 = 22,
  ACT_SECTION_4 = 23,
  ACT_SECTION_5 = 24,
  ACT_SECTION_6 = 25,
  ACT_SECTION_7 = 26,
  ACT_SECTION_8 = 27,
  ACT_SECTION_9 = 28,
  ACT_SECTION_10 = 29,
  ACT_SECTION_11 = 30,
  ACT_SECTION_12 = 31,
  ACT_SECTION_13 = 32,
  ACT_SECTION_14 = 33,
  ACT_SECTION_15 = 34,
  ACT_SECTION_16 = 35
};

enum CabMode : uint8_t
{
  MODE_OFF = 0,
  MODE_MANUAL = 1,
  MODE_AUTO = 2
};

struct SwitchboxConfig
{
  uint32_t magic;
  uint8_t version;
  uint8_t ethIp[4];
  uint8_t wifiIp[4];
  uint8_t gateway[4];
  uint8_t subnet[4];
  bool wifiStationEnabled;
  char ssid[32];
  char password[32];
  char apName[32];
  char apPassword[32];
  bool inputActiveLow;
  uint16_t sendPeriodMs;
  uint16_t pulseMs;
  uint8_t inputAction[16];
  bool overrideSectionSwitchesOn;
  uint8_t buzzerVolume;
};

SwitchboxConfig Cfg;

EthernetUDP UdpEth;
EthernetServer EthServer(WEB_PORT);
WiFiUDP UdpWifi;
WebServer WifiServer(WEB_PORT);
DNSServer DnsServer;

bool EthernetHardwareFound = false;
bool McpFound = false;
bool LastInputActive[16] = { false };
bool InputActive[16] = { false };
uint16_t RawMcpInputs = 0;
CabMode Mode = MODE_OFF;
uint32_t LastSendMs = 0;
uint32_t MasterOnPulseUntil = 0;
uint32_t MasterOffPulseUntil = 0;
uint32_t RateUpPulseUntil = 0;
uint32_t RateDownPulseUntil = 0;
uint32_t BuzzerTestUntil = 0;
uint32_t LastAlarmPacketMs = 0;
uint8_t AlarmStatus = 0;
uint8_t AlarmPattern = 0;
bool BuzzerOutputOn = false;

uint8_t NormalizeBuzzerLevel(uint8_t raw)
{
  if (raw <= 2) return raw;
  if (raw == 0) return 0;
  if (raw < 67) return 1;
  return 2;
}

const char* BuzzerLevelName(uint8_t level)
{
  switch (NormalizeBuzzerLevel(level))
  {
  case 0: return "Off";
  case 1: return "Reduced";
  default: return "Full";
  }
}

String DefaultApName()
{
  uint64_t mac = ESP.getEfuseMac();
  uint16_t suffix = (uint16_t)(mac & 0xFFFF);
  char name[32];
  snprintf(name, sizeof(name), "CabSwitchbox-%04X", suffix);
  return String(name);
}

void ApplyDefaultApNameIfNeeded()
{
  String current = String(Cfg.apName);
  if (current.length() == 0 || current == "CabSwitchbox")
  {
    String apName = DefaultApName();
    apName.toCharArray(Cfg.apName, sizeof(Cfg.apName));
  }
}

const char* ActionName(uint8_t action)
{
  switch (action)
  {
  case ACT_DISABLED: return "Disabled";
  case ACT_AUTO_MODE: return "Auto mode";
  case ACT_MANUAL_MODE: return "Manual mode";
  case ACT_OFF_MODE: return "Off mode";
  case ACT_MASTER_ON: return "Master on";
  case ACT_MASTER_OFF: return "Master off";
  case ACT_RATE_UP: return "Rate up";
  case ACT_RATE_DOWN: return "Rate down";
  case ACT_WORK_SWITCH: return "Work switch";
  case ACT_AUTO_SECTION: return "Manual section override";
  case ACT_AUTO_RATE: return "Manual rate override";
  case ACT_RATE_2: return "Rate 2";
  case ACT_MASTER_SWITCH: return "Master";
  case ACT_SECTION_1: return "Section 1";
  case ACT_SECTION_2: return "Section 2";
  case ACT_SECTION_3: return "Section 3";
  case ACT_SECTION_4: return "Section 4";
  case ACT_SECTION_5: return "Section 5";
  case ACT_SECTION_6: return "Section 6";
  case ACT_SECTION_7: return "Section 7";
  case ACT_SECTION_8: return "Section 8";
  case ACT_SECTION_9: return "Section 9";
  case ACT_SECTION_10: return "Section 10";
  case ACT_SECTION_11: return "Section 11";
  case ACT_SECTION_12: return "Section 12";
  case ACT_SECTION_13: return "Section 13";
  case ACT_SECTION_14: return "Section 14";
  case ACT_SECTION_15: return "Section 15";
  case ACT_SECTION_16: return "Section 16";
  default: return "Unknown";
  }
}

void SetDefaultConfig()
{
  memset(&Cfg, 0, sizeof(Cfg));
  Cfg.magic = CONFIG_MAGIC;
  Cfg.version = CONFIG_VERSION;
  Cfg.ethIp[0] = 192; Cfg.ethIp[1] = 168; Cfg.ethIp[2] = 0; Cfg.ethIp[3] = 70;
  Cfg.wifiIp[0] = 192; Cfg.wifiIp[1] = 168; Cfg.wifiIp[2] = 0; Cfg.wifiIp[3] = 71;
  Cfg.gateway[0] = 192; Cfg.gateway[1] = 168; Cfg.gateway[2] = 0; Cfg.gateway[3] = 1;
  Cfg.subnet[0] = 255; Cfg.subnet[1] = 255; Cfg.subnet[2] = 255; Cfg.subnet[3] = 0;
  Cfg.wifiStationEnabled = false;
  String apName = DefaultApName();
  apName.toCharArray(Cfg.apName, sizeof(Cfg.apName));
  strncpy(Cfg.apPassword, "", sizeof(Cfg.apPassword) - 1);
  Cfg.inputActiveLow = true;
  Cfg.overrideSectionSwitchesOn = false;
  Cfg.sendPeriodMs = 100;
  Cfg.pulseMs = 250;
  Cfg.buzzerVolume = 60;

  Cfg.inputAction[0] = ACT_MASTER_ON;     // IN0
  Cfg.inputAction[1] = ACT_MASTER_OFF;    // IN1
  Cfg.inputAction[2] = ACT_RATE_UP;       // IN2
  Cfg.inputAction[3] = ACT_RATE_DOWN;     // IN3
  Cfg.inputAction[4] = ACT_AUTO_RATE;     // IN4, asserted = manual rate override
  Cfg.inputAction[5] = ACT_AUTO_SECTION;  // IN5, asserted = manual section override
  Cfg.inputAction[6] = ACT_RATE_2;        // IN6
  Cfg.inputAction[7] = ACT_SECTION_1;     // IN7
  Cfg.inputAction[8] = ACT_SECTION_2;     // IN8
  Cfg.inputAction[9] = ACT_SECTION_3;     // IN9
  Cfg.inputAction[10] = ACT_SECTION_4;    // IN10
  Cfg.inputAction[11] = ACT_SECTION_5;    // IN11
  Cfg.inputAction[12] = ACT_SECTION_6;    // IN12
  Cfg.inputAction[13] = ACT_SECTION_7;    // IN13
  Cfg.inputAction[14] = ACT_SECTION_8;    // IN14
  Cfg.inputAction[15] = ACT_SECTION_9;    // IN15
}

IPAddress ToIp(const uint8_t parts[4])
{
  return IPAddress(parts[0], parts[1], parts[2], parts[3]);
}

IPAddress BroadcastFor(const uint8_t parts[4])
{
  return IPAddress(parts[0], parts[1], parts[2], 255);
}

uint8_t Crc(byte data[], byte length)
{
  uint8_t result = 0;
  for (byte i = 0; i < length; i++) result += data[i];
  return result;
}

void LoadConfig()
{
  EEPROM.get(0, Cfg);
  if (Cfg.magic == CONFIG_MAGIC && Cfg.version == LEGACY_CONFIG_VERSION)
  {
    Cfg.version = CONFIG_VERSION;
    Cfg.overrideSectionSwitchesOn = false;
    EEPROM.put(0, Cfg);
    EEPROM.commit();
  }
  else if (Cfg.magic == CONFIG_MAGIC && Cfg.version == SECTION_SWITCH_REQUIRED_CONFIG_VERSION)
  {
    Cfg.overrideSectionSwitchesOn = !Cfg.overrideSectionSwitchesOn;
    Cfg.version = CONFIG_VERSION;
    EEPROM.put(0, Cfg);
    EEPROM.commit();
  }
  else if (Cfg.magic != CONFIG_MAGIC || Cfg.version != CONFIG_VERSION)
  {
    SetDefaultConfig();
    EEPROM.put(0, Cfg);
    EEPROM.commit();
  }

  if (Cfg.sendPeriodMs < 50 || Cfg.sendPeriodMs > 1000) Cfg.sendPeriodMs = 100;
  if (Cfg.pulseMs < 50 || Cfg.pulseMs > 1000) Cfg.pulseMs = 250;
  Cfg.buzzerVolume = NormalizeBuzzerLevel(Cfg.buzzerVolume);
  Cfg.inputActiveLow = true;
  ApplyDefaultApNameIfNeeded();
}

void SaveConfig()
{
  Cfg.magic = CONFIG_MAGIC;
  Cfg.version = CONFIG_VERSION;
  EEPROM.put(0, Cfg);
  EEPROM.commit();
}

void SetupBuzzer()
{
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

bool AlarmPacketIsLive()
{
  return LastAlarmPacketMs > 0 && (millis() - LastAlarmPacketMs) < ALARM_PACKET_TIMEOUT_MS;
}

bool AlarmIsActive()
{
  return AlarmPacketIsLive() && (AlarmStatus & 0x01);
}

bool AlarmPatternAllowsBeep()
{
  if (!AlarmIsActive()) return false;

  uint32_t phase = millis() % 1000;
  if (AlarmPattern == 2 || (AlarmStatus & 0x02)) return phase < 150 || (phase >= 300 && phase < 450);
  return phase < 350;
}

void DriveBuzzer(bool requestOn)
{
  uint8_t level = NormalizeBuzzerLevel(Cfg.buzzerVolume);
  bool outputOn = false;

  if (requestOn && level > 0)
  {
    if (level >= 2)
    {
      outputOn = true;
    }
    else
    {
      outputOn = (millis() % BUZZER_VOLUME_FRAME_MS) < BUZZER_REDUCED_ON_MS;
    }
  }

  if (outputOn != BuzzerOutputOn)
  {
    BuzzerOutputOn = outputOn;
    digitalWrite(BUZZER_PIN, outputOn ? HIGH : LOW);
  }
}

void UpdateBuzzer()
{
  bool testOn = millis() < BuzzerTestUntil;
  DriveBuzzer(testOn || AlarmPatternAllowsBeep());
}

void StartBuzzerTest()
{
  BuzzerTestUntil = millis() + BUZZER_TEST_MS;
}

void ParseAlarmPacket(byte data[], uint16_t len)
{
  if (len < 5) return;
  if (data[0] != 107 || data[1] != 127) return;
  if (data[4] != Crc(data, 4)) return;

  AlarmStatus = data[2];
  AlarmPattern = data[3];
  LastAlarmPacketMs = millis();
}

void ReceiveAlarmPackets()
{
  byte data[16];

  if (EthernetHardwareFound && Ethernet.linkStatus() == LinkON)
  {
    uint16_t len = UdpEth.parsePacket();
    if (len > 0)
    {
      if (len > sizeof(data)) len = sizeof(data);
      UdpEth.read(data, len);
      ParseAlarmPacket(data, len);
    }
  }

  uint16_t len = UdpWifi.parsePacket();
  if (len > 0)
  {
    if (len > sizeof(data)) len = sizeof(data);
    UdpWifi.read(data, len);
    ParseAlarmPacket(data, len);
  }
}

void SetupMcp23017()
{
  Wire.begin();

  Wire.beginTransmission(MCP23017_ADDRESS);
  McpFound = (Wire.endTransmission() == 0);
  if (!McpFound)
  {
    Serial.println("MCP23017 not found at 0x20.");
    return;
  }

  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(0x00); // IODIRA
  Wire.write(0xFF);
  Wire.write(0xFF); // IODIRB
  Wire.endTransmission();

  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(0x0C); // GPPUA
  Wire.write(0xFF);
  Wire.write(0xFF); // GPPUB
  Wire.endTransmission();

  Serial.println("MCP23017 found at 0x20.");
}

uint16_t ReadMcpInputs()
{
  if (!McpFound) return 0;

  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(0x12); // GPIOA
  if (Wire.endTransmission(false) != 0) return 0;

  if (Wire.requestFrom(MCP23017_ADDRESS, (uint8_t)2) != 2) return 0;
  uint8_t gpioA = Wire.read();
  uint8_t gpioB = Wire.read();
  return (uint16_t)gpioA | ((uint16_t)gpioB << 8);
}

void SetupEthernet()
{
  uint8_t mac[] = { 0xA8, 0x61, 0x0A, 0x70, 0x00, 0x01 };
  Ethernet.init(W5500_CS_PIN);
  Ethernet.begin(mac, ToIp(Cfg.ethIp), ToIp(Cfg.gateway), ToIp(Cfg.gateway), ToIp(Cfg.subnet));
  EthernetHardwareFound = (Ethernet.hardwareStatus() != EthernetNoHardware);

  if (!EthernetHardwareFound)
  {
    Serial.println("W5500 not found; Ethernet disabled.");
    return;
  }

  UdpEth.begin(LOCAL_UDP_PORT);
  EthServer.begin();
  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());
}

void SetupWifi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true);
  delay(100);

  WiFi.softAPConfig(ToIp(Cfg.wifiIp), ToIp(Cfg.wifiIp), ToIp(Cfg.subnet));
  if (strlen(Cfg.apPassword) >= 8) WiFi.softAP(Cfg.apName, Cfg.apPassword);
  else WiFi.softAP(Cfg.apName);
  DnsServer.start(AP_DNS_PORT, "*", ToIp(Cfg.wifiIp));

  if (Cfg.wifiStationEnabled && strlen(Cfg.ssid) > 0)
  {
    WiFi.config(ToIp(Cfg.wifiIp), ToIp(Cfg.gateway), ToIp(Cfg.subnet));
    WiFi.begin(Cfg.ssid, Cfg.password);
  }

  UdpWifi.begin(LOCAL_UDP_PORT);
  WifiServer.on("/", HandleWifiRoot);
  WifiServer.on("/manual", HandleWifiManual);
  WifiServer.on("/save", HTTP_GET, HandleWifiSave);
  WifiServer.on("/status", HandleWifiStatus);
  WifiServer.on("/buzzer-test", HandleWifiBuzzerTest);
  WifiServer.begin();
  if (MDNS.begin(MDNS_NAME))
  {
    MDNS.addService("http", "tcp", WEB_PORT);
    Serial.print("mDNS: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local/");
  }

  Serial.print("WiFi setup IP: ");
  Serial.println(WiFi.softAPIP());
}

void PulseMasterOn()
{
  MasterOnPulseUntil = millis() + Cfg.pulseMs;
  MasterOffPulseUntil = 0;
}

void PulseMasterOff()
{
  MasterOffPulseUntil = millis() + Cfg.pulseMs;
  MasterOnPulseUntil = 0;
}

void SetMode(CabMode mode)
{
  if (Mode == mode) return;

  Mode = mode;
  if (Mode == MODE_OFF) PulseMasterOff();
  else PulseMasterOn();
}

void ApplyInputEdges()
{
  for (uint8_t i = 0; i < 16; i++)
  {
    if (Cfg.inputAction[i] == ACT_MASTER_SWITCH && InputActive[i] != LastInputActive[i])
    {
      if (InputActive[i]) PulseMasterOn();
      else PulseMasterOff();
      continue;
    }

    if (!InputActive[i] || LastInputActive[i]) continue;

    switch (Cfg.inputAction[i])
    {
    case ACT_AUTO_MODE:
      SetMode(MODE_AUTO);
      break;
    case ACT_MANUAL_MODE:
      SetMode(MODE_MANUAL);
      break;
    case ACT_OFF_MODE:
      SetMode(MODE_OFF);
      break;
    case ACT_MASTER_ON:
      PulseMasterOn();
      break;
    case ACT_MASTER_OFF:
      PulseMasterOff();
      break;
    case ACT_RATE_UP:
      RateUpPulseUntil = millis() + Cfg.pulseMs;
      RateDownPulseUntil = 0;
      break;
    case ACT_RATE_DOWN:
      RateDownPulseUntil = millis() + Cfg.pulseMs;
      RateUpPulseUntil = 0;
      break;
    }
  }

  memcpy(LastInputActive, InputActive, sizeof(InputActive));
}

bool AnyActionActive(uint8_t action)
{
  for (uint8_t i = 0; i < 16; i++)
  {
    if (Cfg.inputAction[i] == action && InputActive[i]) return true;
  }
  return false;
}

void ReadInputs()
{
  uint16_t raw = ReadMcpInputs();
  RawMcpInputs = raw;
  for (uint8_t i = 0; i < 16; i++)
  {
    bool high = ((raw & (1 << i)) != 0);
    InputActive[i] = !high;
  }
  ApplyInputEdges();
}

void BuildSwitchboxPacket(byte data[6])
{
  uint32_t now = millis();
  byte status = 0;
  byte sectionsLo = 0;
  byte sectionsHi = 0;

  if (now < MasterOnPulseUntil) status |= 0b00000010;
  if (now < MasterOffPulseUntil) status |= 0b00000100;
  if (now < RateUpPulseUntil || AnyActionActive(ACT_RATE_UP)) status |= 0b00001000;
  if (now < RateDownPulseUntil || AnyActionActive(ACT_RATE_DOWN)) status |= 0b00010000;

  if (AnyActionActive(ACT_RATE_2)) status |= 0b00000001;
  if (Mode == MODE_AUTO || !AnyActionActive(ACT_AUTO_SECTION)) status |= 0b00100000;
  if (Mode == MODE_AUTO || !AnyActionActive(ACT_AUTO_RATE)) status |= 0b01000000;
  if (AnyActionActive(ACT_WORK_SWITCH)) status |= 0b10000000;

  for (uint8_t i = 0; i < 16; i++)
  {
    uint8_t action = Cfg.inputAction[i];
    if (action >= ACT_SECTION_1 && action <= ACT_SECTION_16 && (Cfg.overrideSectionSwitchesOn || InputActive[i]))
    {
      uint8_t section = action - ACT_SECTION_1;
      if (section < 8) sectionsLo |= 1 << section;
      else sectionsHi |= 1 << (section - 8);
    }
  }

  data[0] = 106;
  data[1] = 127;
  data[2] = status;
  data[3] = sectionsLo;
  data[4] = sectionsHi;
  data[5] = Crc(data, 5);
}

void SendSwitchboxPacket()
{
  if (millis() - LastSendMs < Cfg.sendPeriodMs) return;
  LastSendMs = millis();

  byte data[6];
  BuildSwitchboxPacket(data);

  if (EthernetHardwareFound && Ethernet.linkStatus() == LinkON)
  {
    UdpEth.beginPacket(BroadcastFor(Cfg.ethIp), RATE_CONTROLLER_PORT);
    UdpEth.write(data, sizeof(data));
    UdpEth.endPacket();
  }

  UdpWifi.beginPacket(BroadcastFor(Cfg.wifiIp), RATE_CONTROLLER_PORT);
  UdpWifi.write(data, sizeof(data));
  UdpWifi.endPacket();
}

void SetupWeb()
{
}

String IpFields(const char* name, const uint8_t ip[4])
{
  String s;
  for (uint8_t i = 0; i < 4; i++)
  {
    s += "<input name='";
    s += name;
    s += i;
    s += "' type='number' min='0' max='255' value='";
    s += ip[i];
    s += "'>";
  }
  return s;
}

String ActionOptions(uint8_t selected)
{
  const uint8_t actions[] = {
    ACT_DISABLED, ACT_MASTER_ON, ACT_MASTER_OFF,
    ACT_RATE_UP, ACT_RATE_DOWN, ACT_WORK_SWITCH, ACT_AUTO_SECTION, ACT_AUTO_RATE, ACT_RATE_2,
    ACT_MASTER_SWITCH,
    ACT_SECTION_1, ACT_SECTION_2, ACT_SECTION_3, ACT_SECTION_4, ACT_SECTION_5, ACT_SECTION_6,
    ACT_SECTION_7, ACT_SECTION_8, ACT_SECTION_9, ACT_SECTION_10, ACT_SECTION_11, ACT_SECTION_12,
    ACT_SECTION_13, ACT_SECTION_14, ACT_SECTION_15, ACT_SECTION_16
  };

  String s;
  for (uint8_t i = 0; i < sizeof(actions); i++)
  {
    uint8_t action = actions[i];
    s += "<option value='";
    s += action;
    s += "'";
    if (action == selected) s += " selected";
    s += ">";
    s += ActionName(action);
    s += "</option>";
  }
  return s;
}

String StatusJson()
{
  byte data[6];
  BuildSwitchboxPacket(data);

  String s = "{";
  s += "\"mcp\":";
  s += McpFound ? "true" : "false";
  s += ",\"ethernet\":";
  s += (EthernetHardwareFound && Ethernet.linkStatus() == LinkON) ? "true" : "false";
  s += ",\"wifiStation\":";
  s += WiFi.isConnected() ? "true" : "false";
  s += ",\"wifiRssi\":";
  s += WiFi.isConnected() ? WiFi.RSSI() : 0;
  s += ",\"mode\":";
  s += Mode;
  s += ",\"rawMcp\":";
  s += RawMcpInputs;
  s += ",\"rawA\":";
  s += (RawMcpInputs & 0xFF);
  s += ",\"rawB\":";
  s += (RawMcpInputs >> 8);
  s += ",\"status\":";
  s += data[2];
  s += ",\"crc\":";
  s += data[5];
  s += ",\"masterOn\":";
  s += (data[2] & 0b00000010) ? "true" : "false";
  s += ",\"masterOff\":";
  s += (data[2] & 0b00000100) ? "true" : "false";
  s += ",\"rateUp\":";
  s += (data[2] & 0b00001000) ? "true" : "false";
  s += ",\"rateDown\":";
  s += (data[2] & 0b00010000) ? "true" : "false";
  s += ",\"autoSection\":";
  s += (data[2] & 0b00100000) ? "true" : "false";
  s += ",\"autoRate\":";
  s += (data[2] & 0b01000000) ? "true" : "false";
  s += ",\"rate2\":";
  s += (data[2] & 0b00000001) ? "true" : "false";
  s += ",\"work\":";
  s += (data[2] & 0b10000000) ? "true" : "false";
  s += ",\"sectionsLo\":";
  s += data[3];
  s += ",\"sectionsHi\":";
  s += data[4];
  s += ",\"overrideSectionSwitchesOn\":";
  s += Cfg.overrideSectionSwitchesOn ? "true" : "false";
  s += ",\"buzzerLevel\":";
  s += NormalizeBuzzerLevel(Cfg.buzzerVolume);
  s += ",\"buzzerLevelName\":\"";
  s += BuzzerLevelName(Cfg.buzzerVolume);
  s += "\"";
  s += ",\"buzzerOn\":";
  s += BuzzerOutputOn ? "true" : "false";
  s += ",\"buzzerTest\":";
  s += (millis() < BuzzerTestUntil) ? "true" : "false";
  s += ",\"alarmActive\":";
  s += AlarmIsActive() ? "true" : "false";
  s += ",\"alarmLive\":";
  s += AlarmPacketIsLive() ? "true" : "false";
  s += ",\"alarmStatus\":";
  s += AlarmStatus;
  s += ",\"alarmPattern\":";
  s += AlarmPattern;
  s += ",\"packet\":[";
  for (uint8_t i = 0; i < 6; i++)
  {
    if (i) s += ",";
    s += data[i];
  }
  s += "]";
  s += ",\"actions\":[";
  for (uint8_t i = 0; i < 16; i++)
  {
    if (i) s += ",";
    s += "\"";
    s += ActionName(Cfg.inputAction[i]);
    s += "\"";
  }
  s += "]";
  s += ",\"inputs\":[";
  for (uint8_t i = 0; i < 16; i++)
  {
    if (i) s += ",";
    s += InputActive[i] ? "true" : "false";
  }
  s += "]}";
  return s;
}

String Page()
{
  String s;
  s += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Cab Switchbox</title><style>";
  s += "body{font-family:Arial,sans-serif;margin:0;background:#f5f6f7;color:#1f2933}header{background:#263238;color:white;padding:14px 18px}header a{color:white;text-decoration:none;border:1px solid rgba(255,255,255,.55);border-radius:4px;padding:6px 10px}nav{margin-top:9px}main{max-width:980px;margin:0 auto;padding:14px}section{background:white;border:1px solid #d9dee3;border-radius:6px;margin:12px 0;padding:14px}h1{font-size:22px;margin:0}h2{font-size:18px;margin:0 0 10px}label{display:block;font-weight:bold;margin-top:10px}input,select{font-size:16px;padding:7px;margin:3px;border:1px solid #b8c0c8;border-radius:4px}input[type=number]{width:64px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:8px}.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:7px}.input-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(165px,1fr));gap:7px}.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.pill,.tile{display:inline-block;padding:6px 9px;border-radius:4px;background:#e8eef2;margin:2px}.tile{display:block}.on{background:#b7f0c1}.off{background:#f1c0c0}.muted{color:#52616b;font-size:13px}.mono{font-family:Consolas,monospace}button{font-size:16px;padding:10px 16px;border:0;border-radius:4px;background:#1976d2;color:white}small{color:#52616b}</style>";
  s += "</head><body><header><h1>Cab Switchbox</h1><nav><a href='/manual'>Manual</a></nav></header><main>";
  s += "<section><h2>Status</h2><div id='status'>Loading...</div></section>";
  s += "<form method='get' action='/save'>";

  s += "<section><h2>Network</h2>";
  s += "<label>Ethernet IP</label><div class='row'>" + IpFields("eth", Cfg.ethIp) + "</div>";
  s += "<label>WiFi IP</label><div class='row'>" + IpFields("wifi", Cfg.wifiIp) + "</div>";
  s += "<label>Gateway</label><div class='row'>" + IpFields("gw", Cfg.gateway) + "</div>";
  s += "<label>Subnet</label><div class='row'>" + IpFields("sub", Cfg.subnet) + "</div>";
  s += "<label><input type='checkbox' name='sta'";
  if (Cfg.wifiStationEnabled) s += " checked";
  s += "> Also connect to existing WiFi network</label>";
  s += "<label>Existing WiFi SSID</label><input name='ssid' maxlength='31' value='" + String(Cfg.ssid) + "'>";
  s += "<label>Existing WiFi Password</label><input name='pass' maxlength='31' value='" + String(Cfg.password) + "'>";
  s += "<label>Access Point Name</label><input name='ap' maxlength='31' value='" + String(Cfg.apName) + "'>";
  s += "<label>Access Point Password <small>blank or 8+ chars</small></label><input name='appass' maxlength='31' value='" + String(Cfg.apPassword) + "'>";
  s += "</section>";

  s += "<section><h2>Inputs</h2>";
  s += "<label><input type='checkbox' name='sectionOverride'";
  if (Cfg.overrideSectionSwitchesOn) s += " checked";
  s += "> Override section switches ON</label>";
  s += "<label>Send period ms</label><input name='sendMs' type='number' min='50' max='1000' value='" + String(Cfg.sendPeriodMs) + "'>";
  s += "<label>Pulse length ms</label><input name='pulseMs' type='number' min='50' max='1000' value='" + String(Cfg.pulseMs) + "'>";
  s += "<div class='grid'>";
  for (uint8_t i = 0; i < 16; i++)
  {
    s += "<label>IN";
    s += i;
    s += "<select name='in";
    s += i;
    s += "'>";
    s += ActionOptions(Cfg.inputAction[i]);
    s += "</select></label>";
  }
  s += "</div></section>";
  s += "<section><h2>Alarm Buzzer</h2>";
  s += "<label>Buzzer level</label><select name='buzzerLevel'>";
  s += "<option value='0'";
  if (NormalizeBuzzerLevel(Cfg.buzzerVolume) == 0) s += " selected";
  s += ">Off</option>";
  s += "<option value='1'";
  if (NormalizeBuzzerLevel(Cfg.buzzerVolume) == 1) s += " selected";
  s += ">Reduced</option>";
  s += "<option value='2'";
  if (NormalizeBuzzerLevel(Cfg.buzzerVolume) == 2) s += " selected";
  s += ">Full</option></select>";
  s += "<button type='button' onclick='testBuzzer()'>Test buzzer</button>";
  s += "</section><button type='submit'>Save and restart</button></form>";
  s += "<script>const pins=Array.from({length:16},(_,i)=>'IN'+i);function hx(v){return '0x'+Number(v).toString(16).toUpperCase().padStart(2,'0')}function bit(name,on){return '<span class=\"tile '+(on?'on':'off')+'\">'+name+'<br><span class=\"muted\">'+(on?'ON':'off')+'</span></span>'}function testBuzzer(){fetch('/buzzer-test').then(()=>upd())}function upd(){fetch('/status').then(r=>r.json()).then(j=>{let h='<div class=\"status-grid\">';h+=bit('Input board',j.mcp);h+=bit('Ethernet',j.ethernet);h+=bit('WiFi station',j.wifiStation);h+=bit('Buzzer',j.buzzerOn);h+=bit('Alarm packet',j.alarmLive);h+=bit('Alarm active',j.alarmActive);h+='<span class=\"tile mono\">Buzzer level<br>'+j.buzzerLevelName+'</span>';h+='<span class=\"tile mono\">Packet<br>'+j.packet.map(hx).join(' ')+'</span>';h+='</div><h2>Decoded PGN32618</h2><div class=\"status-grid\">';h+=bit('Master On',j.masterOn);h+=bit('Master Off',j.masterOff);h+=bit('Rate Up',j.rateUp);h+=bit('Rate Down',j.rateDown);h+=bit('Auto Rate',j.autoRate);h+=bit('Auto Section',j.autoSection);h+=bit('Rate 2',j.rate2);h+=bit('Work',j.work);h+='<span class=\"tile mono\">Sections Lo<br>'+hx(j.sectionsLo)+'</span><span class=\"tile mono\">Sections Hi<br>'+hx(j.sectionsHi)+'</span><span class=\"tile mono\">CRC<br>'+hx(j.crc)+'</span></div><h2>Inputs</h2><div class=\"input-grid\">';for(let i=0;i<16;i++)h+='<span class=\"tile '+(j.inputs[i]?'on':'off')+'\"><b>'+pins[i]+'</b> '+(j.inputs[i]?'ON':'off')+'<br><span class=\"muted\">'+j.actions[i]+'</span></span>';h+='</div>';document.getElementById('status').innerHTML=h;});}setInterval(upd,500);upd();</script>";
  s += "</main></body></html>";
  return s;
}

String ManualPage()
{
  String s;
  s += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Cab Switchbox Manual</title><style>";
  s += "body{font-family:Arial,sans-serif;margin:0;background:#f5f6f7;color:#1f2933}header{background:#263238;color:white;padding:14px 18px}header a{color:white;text-decoration:none;border:1px solid rgba(255,255,255,.55);border-radius:4px;padding:6px 10px}nav{margin-top:9px}main{max-width:900px;margin:0 auto;padding:14px}section{background:white;border:1px solid #d9dee3;border-radius:6px;margin:12px 0;padding:14px}h1{font-size:22px;margin:0}h2{font-size:18px;margin:0 0 10px}h3{font-size:16px;margin:14px 0 7px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px}.tile{display:block;padding:8px 10px;border-radius:4px;background:#e8eef2}.mono{font-family:Consolas,monospace}table{border-collapse:collapse;width:100%;font-size:15px}th,td{border:1px solid #c9d3db;padding:7px;text-align:left;vertical-align:top}th{background:#e8eef2}code{background:#eef3f6;padding:1px 4px;border-radius:3px}pre{background:#eef3f6;border-left:4px solid #1976d2;padding:10px;white-space:pre-wrap}</style>";
  s += "</head><body><header><h1>Cab Switchbox Manual</h1><nav><a href='/'>Setup</a></nav></header><main>";

  s += "<section><h2>Power</h2>";
  s += "<p>Power the board from <code>12Vin</code> and <code>GND</code>. Connect supply +12-24 VDC to <code>12Vin</code> and supply negative to <code>GND</code>.</p>";
  s += "<p>The <code>12V</code> terminals are fused outputs from the board input power. Use these fused <code>12V</code> terminals through the operator switches, then return the switched voltage to the desired <code>INx</code> input.</p>";
  s += "<pre>Supply +12-24 VDC  -> 12Vin\nSupply negative    -> GND\nFused 12V terminal -> switch -> INx</pre>";
  s += "<p>The input power fuse is 1 amp. The fused <code>12V</code> terminals are for switch inputs, not heavy loads.</p></section>";

  s += "<section><h2>Additional Parts Needed</h2><table><tr><th>Part</th><th>Notes</th></tr>";
  s += "<tr><td>1 amp mini fuse</td><td>Installs in the fuse holder.</td></tr>";
  s += "<tr><td>ESP32-DEVKitC-VE</td><td>Main controller board.</td></tr>";
  s += "<tr><td>W5500 Ethernet Module</td><td>Ethernet network module.</td></tr>";
  s += "<tr><td>Board-mount buzzer</td><td>Active 12 V buzzer driven by the buzzer MOSFET output.</td></tr></table></section>";

  s += "<section><h2>Default Inputs</h2><table><tr><th>Input</th><th>Default action</th></tr>";
  s += "<tr><td>IN0</td><td>Master on</td></tr><tr><td>IN1</td><td>Master off</td></tr><tr><td>IN2</td><td>Rate up</td></tr><tr><td>IN3</td><td>Rate down</td></tr>";
  s += "<tr><td>IN4</td><td>Manual rate override</td></tr><tr><td>IN5</td><td>Manual section override</td></tr><tr><td>IN6</td><td>Rate 2</td></tr>";
  s += "<tr><td>IN7-IN15</td><td>Section 1 through Section 9</td></tr></table>";
  s += "<p>Any input can be reassigned on the setup page. An input is ON when +12-24 VDC is applied to its terminal.</p></section>";

  s += "<section><h2>Section Switch Override</h2>";
  s += "<p>Enable <b>Override section switches ON</b> when physical section switches are not installed. Inputs assigned to Section 1 through Section 16 are then forced ON in the packet, so no jumper wires are needed.</p>";
  s += "<p>Leave it off when physical section switches are installed.</p></section>";

  s += "<section><h2>Work Switch</h2>";
  s += "<p>The Work switch is normally used for implement up/down sensing. Implement down should feed +12-24 VDC to the assigned Work switch input. Implement up should leave that input unpowered.</p>";
  s += "<p>When Work Switch is enabled in the Rate Controller app, it acts as a master permissive: ON allows application, OFF forces master/sections off. It does not replace Auto Rate or Auto Section.</p></section>";

  s += "<section><h2>Alarm Buzzer Output</h2>";
  s += "<p>The alarm buzzer is driven from ESP32 GPIO27 through a low-side MOSFET. GPIO27 does not need an external pull-up. Use the 100k gate pulldown to keep the buzzer off during boot/reset.</p>";
  s += "<pre>+12V fused power -> buzzer +\nbuzzer - -> MOSFET drain\nMOSFET source -> GND\nGPIO27 -> 100 ohm -> MOSFET gate\nMOSFET gate -> 100k pulldown -> GND</pre>";
  s += "<p>Flyback diode orientation: cathode/stripe to +12V/buzzer +, anode to buzzer -/MOSFET drain.</p></section>";

  s += "<section><h2>Network Access</h2>";
  s += "<div class='grid'><span class='tile'>WiFi AP<br><b>CabSwitchbox</b></span><span class='tile'>WiFi setup IP<br><b>192.168.0.71</b></span><span class='tile'>Ethernet IP<br><b>192.168.0.70</b></span><span class='tile'>mDNS<br><b>cabswitchbox.local</b></span></div>";
  s += "</section></main></body></html>";
  return s;
}

uint8_t ArgOctet(WebServer& server, const String& name, uint8_t fallback)
{
  if (!server.hasArg(name)) return fallback;
  return (uint8_t)constrain(server.arg(name).toInt(), 0, 255);
}

void CopyArg(WebServer& server, const String& name, char* dest, size_t len)
{
  String value = server.hasArg(name) ? server.arg(name) : "";
  value.toCharArray(dest, len);
}

void ApplyPost(WebServer& server)
{
  for (uint8_t i = 0; i < 4; i++)
  {
    Cfg.ethIp[i] = ArgOctet(server, "eth" + String(i), Cfg.ethIp[i]);
    Cfg.wifiIp[i] = ArgOctet(server, "wifi" + String(i), Cfg.wifiIp[i]);
    Cfg.gateway[i] = ArgOctet(server, "gw" + String(i), Cfg.gateway[i]);
    Cfg.subnet[i] = ArgOctet(server, "sub" + String(i), Cfg.subnet[i]);
  }

  Cfg.wifiStationEnabled = server.hasArg("sta");
  Cfg.inputActiveLow = true;
  Cfg.overrideSectionSwitchesOn = server.hasArg("sectionOverride");
  Cfg.sendPeriodMs = constrain(server.arg("sendMs").toInt(), 50, 1000);
  Cfg.pulseMs = constrain(server.arg("pulseMs").toInt(), 50, 1000);
  Cfg.buzzerVolume = NormalizeBuzzerLevel((uint8_t)server.arg("buzzerLevel").toInt());

  CopyArg(server, "ssid", Cfg.ssid, sizeof(Cfg.ssid));
  CopyArg(server, "pass", Cfg.password, sizeof(Cfg.password));
  CopyArg(server, "ap", Cfg.apName, sizeof(Cfg.apName));
  CopyArg(server, "appass", Cfg.apPassword, sizeof(Cfg.apPassword));

  for (uint8_t i = 0; i < 16; i++)
  {
    Cfg.inputAction[i] = (uint8_t)server.arg("in" + String(i)).toInt();
  }

  SaveConfig();
}

String UrlDecode(String value)
{
  value.replace("+", " ");
  String result;
  for (int i = 0; i < value.length(); i++)
  {
    if (value[i] == '%' && i + 2 < value.length())
    {
      char hex[3] = { value[i + 1], value[i + 2], 0 };
      result += (char)strtol(hex, nullptr, 16);
      i += 2;
    }
    else
    {
      result += value[i];
    }
  }
  return result;
}

bool QueryHasKey(const String& query, const String& key)
{
  return query.startsWith(key + "=") || query.indexOf("&" + key + "=") >= 0;
}

String QueryValue(const String& query, const String& key)
{
  String needle = key + "=";
  int start = query.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  int end = query.indexOf('&', start);
  if (end < 0) end = query.length();
  return UrlDecode(query.substring(start, end));
}

uint8_t QueryOctet(const String& query, const String& key, uint8_t fallback)
{
  if (!QueryHasKey(query, key)) return fallback;
  return (uint8_t)constrain(QueryValue(query, key).toInt(), 0, 255);
}

void CopyQuery(const String& query, const String& key, char* dest, size_t len)
{
  String value = QueryValue(query, key);
  value.toCharArray(dest, len);
}

void ApplyQuery(const String& query)
{
  for (uint8_t i = 0; i < 4; i++)
  {
    Cfg.ethIp[i] = QueryOctet(query, "eth" + String(i), Cfg.ethIp[i]);
    Cfg.wifiIp[i] = QueryOctet(query, "wifi" + String(i), Cfg.wifiIp[i]);
    Cfg.gateway[i] = QueryOctet(query, "gw" + String(i), Cfg.gateway[i]);
    Cfg.subnet[i] = QueryOctet(query, "sub" + String(i), Cfg.subnet[i]);
  }

  Cfg.wifiStationEnabled = QueryHasKey(query, "sta");
  Cfg.inputActiveLow = true;
  Cfg.overrideSectionSwitchesOn = QueryHasKey(query, "sectionOverride");
  if (QueryHasKey(query, "sendMs")) Cfg.sendPeriodMs = constrain(QueryValue(query, "sendMs").toInt(), 50, 1000);
  if (QueryHasKey(query, "pulseMs")) Cfg.pulseMs = constrain(QueryValue(query, "pulseMs").toInt(), 50, 1000);
  if (QueryHasKey(query, "buzzerLevel")) Cfg.buzzerVolume = NormalizeBuzzerLevel((uint8_t)QueryValue(query, "buzzerLevel").toInt());

  CopyQuery(query, "ssid", Cfg.ssid, sizeof(Cfg.ssid));
  CopyQuery(query, "pass", Cfg.password, sizeof(Cfg.password));
  CopyQuery(query, "ap", Cfg.apName, sizeof(Cfg.apName));
  CopyQuery(query, "appass", Cfg.apPassword, sizeof(Cfg.apPassword));

  for (uint8_t i = 0; i < 16; i++)
  {
    String key = "in" + String(i);
    if (QueryHasKey(query, key)) Cfg.inputAction[i] = (uint8_t)QueryValue(query, key).toInt();
  }

  SaveConfig();
}

void HandleWifiRoot()
{
  WifiServer.send(200, "text/html", Page());
}

void HandleWifiManual()
{
  WifiServer.send(200, "text/html", ManualPage());
}

void HandleWifiStatus()
{
  WifiServer.send(200, "application/json", StatusJson());
}

void HandleWifiBuzzerTest()
{
  StartBuzzerTest();
  WifiServer.send(200, "application/json", "{\"buzzerTest\":true}");
}

void HandleWifiSave()
{
  ApplyPost(WifiServer);
  WifiServer.send(200, "text/html", "<html><body><h1>Saved</h1><p>Restarting...</p></body></html>");
  delay(400);
  ESP.restart();
}

String HttpToken(const String& text, int index)
{
  int start = 0;
  for (int i = 0; i < index; i++)
  {
    start = text.indexOf(' ', start);
    if (start < 0) return "";
    start++;
  }
  int end = text.indexOf(' ', start);
  if (end < 0) end = text.length();
  return text.substring(start, end);
}

void SendEthResponse(EthernetClient& client, const String& body, const char* contentType, int code)
{
  client.print("HTTP/1.1 ");
  client.print(code);
  client.println(code == 200 ? " OK" : " No Content");
  client.print("Content-Type: ");
  client.println(contentType);
  client.print("Content-Length: ");
  client.println(body.length());
  client.println("Connection: close");
  client.println();
  client.print(body);
}

void HandleEthernetWeb()
{
  if (!EthernetHardwareFound || Ethernet.linkStatus() != LinkON) return;

  EthernetClient client = EthServer.available();
  if (!client) return;

  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  while (client.connected() && client.available())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  String target = HttpToken(requestLine, 1);
  String path = target;
  String query = "";
  int queryAt = target.indexOf('?');
  if (queryAt >= 0)
  {
    path = target.substring(0, queryAt);
    query = target.substring(queryAt + 1);
  }

  if (path == "/status")
  {
    SendEthResponse(client, StatusJson(), "application/json", 200);
  }
  else if (path == "/buzzer-test")
  {
    StartBuzzerTest();
    SendEthResponse(client, "{\"buzzerTest\":true}", "application/json", 200);
  }
  else if (path == "/manual")
  {
    SendEthResponse(client, ManualPage(), "text/html", 200);
  }
  else if (path == "/save")
  {
    ApplyQuery(query);
    SendEthResponse(client, "<html><body><h1>Saved</h1><p>Restarting...</p></body></html>", "text/html", 200);
    delay(400);
    ESP.restart();
  }
  else
  {
    SendEthResponse(client, Page(), "text/html", 200);
  }
  delay(1);
  client.stop();
}

void setup()
{
  Serial.begin(38400);
  delay(250);
  Serial.println();
  Serial.println(INO_DESCRIPTION);

  EEPROM.begin(EEPROM_SIZE);
  LoadConfig();

  SetupBuzzer();
  SetupMcp23017();
  SetupEthernet();
  SetupWifi();
}

void loop()
{
  DnsServer.processNextRequest();
  WifiServer.handleClient();
  HandleEthernetWeb();
  ReadInputs();
  ReceiveAlarmPackets();
  SendSwitchboxPacket();
  UpdateBuzzer();
}
