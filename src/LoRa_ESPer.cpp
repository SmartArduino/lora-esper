#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
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
// .local will be appended, so "lora" becomes "lora.local"
const char *host = "lora";
const size_t packet_buffer_length = 50;
const size_t json_buffer_size = JSON_ARRAY_SIZE(3) + packet_buffer_length * JSON_OBJECT_SIZE(3);
ESP8266WebServer server(80);


/*
 * Volatile variables
 */
byte init_error = 0;
lora_packet* packets[packet_buffer_length + 1];
size_t p_pos = 0;
int sync_word = 0x34;
String flash_bag = "";


/*
 *  Method definitions
 */
// Web handlers
void handleRoot();
void handleJson();
void handleChannelGet();
void handleChannelPost();
// Web utilities
String getFlashbag();
String buildPacketsJson();
// Packet buffer utiliities
void pushPacket(lora_packet packet);
// LoRa handler
void onLoRaReceive(int packet_size);
// Inits
void initWiFiAP();
void initWebServer();
void initLoRa();


/*
 * Address: http://192.168.4.1
 */
void handleRoot() {
  if (init_error == 0) {
    server.send(200, "text/html", getFlashbag() +
      "<h1>You are connected</h1><br /><br /><p>" +
      "<a href='/json'><code>/json</code></a> - Show last received LoRa packets in JSON.<br />" +
      "<a href='/syncword'><code>/syncword</code></a> - Show and change the sync word."
      "</p>"
    );
  } else {
   char error_msg[39];
   sprintf(error_msg, "Error during initialization (code 0x%#02x)", init_error);
   server.send(200, "text/html", error_msg);
  }
}

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
    getFlashbag() +
    "<p>Current sync word: <code>" + sw_name + "</code></p><br />" +
    "<p>Public sync word: <code>0x34 (52)</code><br />Private (LoRaWAN) sync word: <code>0x12 (18)</code><br />Allowed: 1-byte integer (between 0 and 255)</p><br />" +
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
        flash_bag += "Sync word set to " + String(sw_i_new) + ".\n";
      } else {
        flash_bag += "Sync word must be between 0 and 255.\n";
      }
    }
  }

  server.sendHeader("Location", "/syncword", true);
  server.send(302, "text/plain", "");
}

String getFlashbag() {
  if (flash_bag.length() > 0) {
    String flash_bag_content = "<style>ul.flashbag{padding:0;list-style:none}ul.flashbag li{padding:10px;background:#efeeee;border:1px solid white}</style><ul class='flashbag'><li>";
    flash_bag.replace("\n", "</li><li>");
    flash_bag_content += flash_bag.substring(0, flash_bag.lastIndexOf("<li>"));
    flash_bag_content += "</ul><br />";
    flash_bag = "";

    return flash_bag_content;
  }

  return "";
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

  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.on("/syncword", HTTP_GET, handleSyncWordGet);
  server.on("/syncword", HTTP_POST, handleSyncWordPost);
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
    init_error = 1;
  } else {
    LoRa.setSyncWord(sync_word);
    LoRa.onReceive(onLoRaReceive);
    LoRa.receive();
  }
}

void setup() {
	Serial.begin(115200);
	while (!Serial);
  delay(300);
  Serial.println();
	Serial.print("LoRa ESPer Bridge ");
  Serial.println(version);
  Serial.println();

  initLoRa();
  initWiFiAP();
  initWebServer();
}

void loop() {
  while (1) server.handleClient();
}
