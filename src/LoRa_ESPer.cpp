#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LoRa.h>


/*
 * Structures
 */
 // lora_packet.data MUST be declared LAST
typedef struct lora_packet {
  float snr;
  int rssi;
  char data[];
} lora_packet;


/*
 * Constants
 */
const char *version = "v0.2";
const char *ssid = "LoRa ESPer";
const char *host = "lora";  // .local will be appended, so "lora" becomes "lora.local"
IPAddress host_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
WebServer server(80);
const size_t packet_buffer_length = 50;
const size_t json_buffer_size = JSON_ARRAY_SIZE(3) + packet_buffer_length * JSON_OBJECT_SIZE(3);


/*
 * Volatile variables
 */
lora_packet* packets[packet_buffer_length + 1];
size_t p_pos = 0;
int sync_word = 0x34;


/*
 *  Method definitions
 */
// Web handlers
void handleRoot();
void handleJson();
void handleChannelGet();
void handleChannelPost();
// Web utilities
String buildPacketsJson();
// Packet buffer utiliities
void pushPacket(lora_packet packet);
// LoRa handler
void onLoRaReceive(int packet_size);
// Inits
void initWiFiAP();
void initWebServer();
void initLoRa();


void handleJson() {
  server.send(200, "application/json", buildPacketsJson());
}

void handleSyncWordGet() {
  String sw_name = "default";
  if (sync_word > -1) {
    char sw_buff[9];
    sprintf(sw_buff, "0x%#02X (%u)", sync_word, sync_word);
    sw_name = String(sw_buff);
  }

  server.send(200, "text/html",
    "<p>Current sync word: <code>" + sw_name + "</code></p><br />"
    "<p>Public sync word: <code>0x34 (52)</code><br />Private (LoRaWAN) sync word: <code>0x12 (18)</code><br />Allowed: 1-byte integer (between 0 and 255)</p><br />"
    "<form method='post'><p>Set new sync word (dec): <input name='sync_word' value='" + (sync_word > -1 ? String(sync_word) : "") + "' /> <input type='submit' name='submit' value='submit' /></p></form>"
  );
}

void handleSyncWordPost() {
  if (server.hasArg("sync_word")) {
    String sw_s_new = server.arg("sync_word");

    if (sw_s_new.length() > 0) {
      int sw_i_new = sw_s_new.toInt();
      // We only allow 1-byte sync words for now
      if (sw_i_new > -1 && sw_i_new < 256) {
        sync_word = sw_i_new;
        LoRa.setSyncWord(sync_word);
        char flash[30];
        sprintf(flash, "Sync word set to 0x%#02X (%u).", sw_i_new, sw_i_new);
        server.addFlash("success", flash);
      } else {
        server.addFlash("error", "Sync word must be between 0 and 255.");
      }
    }
  }

  server.sendHeader("Location", "/syncword", true);
  server.send(302, "text/plain", "");
}

String buildPacketsJson() {
  String json_str;
  DynamicJsonBuffer json(json_buffer_size);
  JsonArray& json_root = json.createArray();

  size_t i = ((p_pos - 1) % packet_buffer_length), last = p_pos;
  lora_packet* packet = packets[i];
  while (i != p_pos && packet) {
    JsonObject& j_pack = json_root.createNestedObject();
    j_pack["rssi"] = packet->rssi;
    j_pack["snr"] = packet->snr;
    j_pack["data"] = String(packet->data);

    i = (i - 1) % packet_buffer_length;
    packet = packets[i];
  }

  json_root.printTo(json_str);
  json.clear();

  return json_str;
}

void pushPacket(lora_packet* packet) {
  p_pos = (p_pos + 1) % packet_buffer_length;
  if (packets[p_pos]) free(packets[p_pos]);
  packets[p_pos] = packet;
}

void onLoRaReceive(int packet_size) {
  if (packet_size == 0) return;

  lora_packet* packet = (lora_packet*) malloc(sizeof(lora_packet) + packet_size + 1);

  size_t i = 0;
  for (;i < packet_size && LoRa.available(); i++) packet->data[i] = LoRa.read();
  packet->data[i] = '\0';
  packet->rssi = LoRa.packetRssi();
  packet->snr = LoRa.packetSnr();

  pushPacket(packet);

  Serial.print("Received LoRa packet. RSSI ");
  Serial.print(packet->rssi);
  Serial.print(", SNR ");
  Serial.print(packet->snr);
  Serial.print(", size ");
  Serial.print(packet_size);
  Serial.print(", data: ");
  Serial.println(packet->data);
}

void initWiFiAP() {
  WiFi.hostname(host);
  WiFi.softAPConfig(host_ip, gateway, subnet);
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(ssid)) {
    IPAddress hostIP = WiFi.softAPIP();

    Serial.print("AP IP address: ");
    Serial.println(hostIP);

    if (MDNS.begin(host)) {
      MDNS.addService("http", "tcp", 80);
      Serial.print("mDNS hostname: ");
      Serial.print(host);
      Serial.println(".local");
    } else {
      Serial.println("!! WARN: Failed to start mDNS");
    }
  } else {
    Serial.println("!! ERR: Failed to start WiFi AP");
  }
}

void initWebServer() {
  Serial.print("JSON Buffer size: ");
  Serial.println(json_buffer_size);

  server.addEndpoint("/json", "Show last received LoRa packets in JSON.", handleJson);
  server.addEndpoint("/syncword", "Show and change the sync word.", HTTP_GET, handleSyncWordGet);
  server.addEndpoint("/syncword", "", HTTP_POST, handleSyncWordPost);
  server.begin();
  Serial.println("HTTP server started: /, /json, /syncword [GET,POST]");
}

void initLoRa() {
  SPI.setFrequency(4E6);
  LoRa.setSPIFrequency(4E6);
  // NSS = D8, Reset = D0,  DIO0 = D1
  LoRa.setPins(15, 16, 5);

  if (!LoRa.begin(433E6)) {
    Serial.println("!! ERR: Failed to start LoRa");
    server.setIndexContentPrefix("<h2 style='color: red'>Error during initialization of the LoRa module.</h2>");
  } else {
    LoRa.setSyncWord(sync_word);
    LoRa.onReceive(onLoRaReceive);
    LoRa.receive();
  }
}

void setup() {
	Serial.begin(115200);
	while (!Serial);
  delay(100);

  String hello_message = "Welcome to the LoRa ESPer Bridge ";
  hello_message.concat(version);
  Serial.println();
	Serial.print(hello_message);
  Serial.println();
  hello_message = "<p><b>" + hello_message;
  hello_message.concat("</b></p><br /><br />");
  server.setIndexContentPrefix(hello_message.c_str());

  initLoRa();
  initWiFiAP();
  initWebServer();
}

void loop() {
  while (1) server.handleClient();
}
