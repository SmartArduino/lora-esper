#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <LoRa.h>


/*
 * Structures
 */
// lora_packet.data MUST be declared LAST
typedef struct lora_packet {
    float snr;
    int rssi;
    size_t size;
    char data[];
} lora_packet;


/*
 * Constants
 */
const char *version = "v0.3";
const char *ssid = "LoRa ESPer";
const char *host = "lora";  // .local will be appended, so "lora" becomes "lora.local"
const char *host_with_local = "lora.local";
IPAddress host_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
WebServer server(host_with_local, host_ip, 80);
DNSServer dns;
const unsigned long lora_frequency = (unsigned long) 8681e5;
const size_t packet_buffer_length = 50;
const size_t json_buffer_size = JSON_ARRAY_SIZE(3) + packet_buffer_length * JSON_OBJECT_SIZE(3);
const uint8_t JSON_DATA_FORMAT_RAW = 0;
const uint8_t JSON_DATA_FORMAT_BIN = 1;
const uint8_t JSON_DATA_FORMAT_HEX = 2;
const uint8_t JSON_DATA_FORMAT_BASE64 = 3;


/*
 * Volatile variables
 */
lora_packet *packets[packet_buffer_length + 1];
size_t p_pos = 0;
int sync_word = 0x34;


/*
 *  Method definitions
 */
// Web handlers
void handleJson();
void handleSyncWordGet();
void handleSyncWordPost();
// Web utilities
String buildPacketsJson(uint8_t format = JSON_DATA_FORMAT_BASE64);
String encodeToFormat(char *buffer, size_t length, uint8_t format = JSON_DATA_FORMAT_RAW);
String byteArrayToHexString(const char *buffer, size_t length);
String byteArrayToBinString(const char *buffer, size_t length);
// Packet buffer utilities
void pushPacket(lora_packet *packet);
// LoRa handler
void onLoRaReceive(int packet_size);
// Inits
void initDNS();
void initWiFiAP();
void initWebServer();
void initLoRa(unsigned long frequency = (unsigned long) 868E6);


void handleJson() {
    uint8_t format = JSON_DATA_FORMAT_RAW;
    String formatArg = server.arg("format");
    if (formatArg.equals("base64")) {
        format = JSON_DATA_FORMAT_BASE64;
    } else if (formatArg.equals("hex")) {
        format = JSON_DATA_FORMAT_HEX;
    } else if (formatArg.equals("bin")) {
        format = JSON_DATA_FORMAT_BIN;
    }

    String json = buildPacketsJson(format);
    server.send_P(200, "application/json", json.c_str(), json.length());
}

void handleSyncWordGet() {
    String sw_name = "default";
    if (sync_word > -1) {
        char sw_buff[9];
        sprintf(sw_buff, "0x%02X (%u)", sync_word, sync_word);
        sw_name = String(sw_buff);
    }

  server.send(200, "text/html",
      "<p>Current sync word: <code>" + sw_name + "</code></p><br />"
      "<p>Public sync word: <code>0x34 (52)</code><br />Private (Multitech LoRaWAN) sync word: <code>0x12 (18)</code><br />Allowed: 1-byte integer (between 0 and 255)</p><br />"
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
                sprintf(flash, "Sync word set to 0x%02X (%u).", sw_i_new, sw_i_new);
                server.addFlash("success", flash);
            } else {
                server.addFlash("error", "Sync word must be between 0 and 255.");
            }
        }
    }

    server.sendHeader("Location", "/syncword", true);
    server.send(302, "text/plain", "");
}

String buildPacketsJson(uint8_t format) {
    String json_str;
    DynamicJsonBuffer json(json_buffer_size);
    JsonArray &json_root = json.createArray();

    size_t i = p_pos, last = (p_pos + 1) % packet_buffer_length;
    lora_packet *packet = packets[i];
    while (i != last && packet) {
        JsonObject &j_pack = json_root.createNestedObject();
        j_pack["rssi"] = packet->rssi;
        j_pack["snr"] = packet->snr;
        j_pack["size"] = packet->size;
        j_pack["data"] = encodeToFormat(packet->data, packet->size, format);
        i = (i - 1) % packet_buffer_length;
        packet = packets[i];
    }

    json_root.printTo(json_str);
    json.clear();

    return json_str;
}

String encodeToFormat(char *buffer, size_t length, const uint8_t format) {
    switch (format) {
        case JSON_DATA_FORMAT_BASE64:
            return base64::encode((uint8_t *) buffer, length, false);

        case JSON_DATA_FORMAT_HEX:
            return byteArrayToHexString(buffer, length);

        case JSON_DATA_FORMAT_BIN:
            return byteArrayToBinString(buffer, length);

        default:
        case JSON_DATA_FORMAT_RAW:
            return String(buffer);
    }
}

String byteArrayToHexString(const char *buffer, size_t length) {
    size_t newLength = length * 2;
    char out[newLength + 1];
    char c;
    for (size_t i = 0; i < length; i++) {
        c = (buffer[i] >> 4) & 0x0F;
        out[i * 2] = c + (c > 9 ? 55 : 48);
        c = buffer[i] & 0x0F;
        out[(i * 2) + 1] = c + (c > 9 ? 55 : 48);
    }
    out[newLength] = '\0';

    return String(out);
}

String byteArrayToBinString(const char *buffer, size_t length) {
    size_t newLength = length * 8;
    char out[newLength + 1];
    for (size_t i = 0; i < length; i++) {
        for (short j = 7; j >= 0; j--) {
            out[(i * 8) + (7 - j)] = (char) (((buffer[i] >> j) & 0x01) + 48);
        }
    }
    out[newLength] = '\0';

    return String(out);
}

void pushPacket(lora_packet *packet) {
    p_pos = (p_pos + 1) % packet_buffer_length;
    if (packets[p_pos]) free(packets[p_pos]);
    packets[p_pos] = packet;
}

void onLoRaReceive(int packet_size) {
    if (packet_size == 0) return;

    lora_packet *packet = (lora_packet *) malloc(sizeof(lora_packet) + packet_size + 1);

    size_t i = 0;
    for (; i < packet_size && LoRa.available(); i++) packet->data[i] = (char) LoRa.read();
    packet->data[i] = '\0';
    packet->size = i;
    packet->rssi = LoRa.packetRssi();
    packet->snr = LoRa.packetSnr();

    pushPacket(packet);

    Serial.print("Received LoRa packet. RSSI ");
    Serial.print(packet->rssi);
    Serial.print(", SNR ");
    Serial.print(packet->snr);
    Serial.print(", size ");
    Serial.print(packet_size);
    Serial.print(", actual size ");
    Serial.println(packet->size);
}

void initDNS() {
    delay(50);

    dns.setTTL(60);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (dns.start(53, "*", host_ip)) {
        Serial.print("DNS started, announcing * as ");
        Serial.println(host_ip);
    } else {
        Serial.println("!! WARN: Failed to start DNS");
    }

    if (MDNS.begin(host)) {
        MDNS.addService("http", "tcp", 80);
        Serial.print("mDNS hostname: ");
        Serial.print(host);
        Serial.println(".local");
    } else {
        Serial.println("!! WARN: Failed to start mDNS");
    }
}

void initWiFiAP() {
    WiFi.hostname(host);
    WiFi.softAPConfig(host_ip, gateway, subnet);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(ssid)) {
        host_ip = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(host_ip);

        initDNS();
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

void initLoRa(unsigned long frequency) {
    SPI.setFrequency(4E6);
    LoRa.setSPIFrequency(4E6);
    // NSS = D8, Reset = D0,  DIO0 = D1
    LoRa.setPins(15, 16, 5);

    if (!LoRa.begin(frequency)) {
        Serial.println("!! ERR: Failed to start LoRa");
        server.setIndexContentPrefix("<h2 style='color: red'>Error during initialization of the LoRa module.</h2>");
    } else {
        LoRa.setSyncWord(sync_word);
        LoRa.onReceive(onLoRaReceive);
        LoRa.receive();
        Serial.print("LoRa module initialized with ");
        Serial.print(frequency);
        Serial.println("Hz");
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

    initLoRa(lora_frequency);
    initWiFiAP();
    initWebServer();
}

void loop() {
    while (1) {
        dns.processNextRequest();
        server.handleClient();
    }
}
