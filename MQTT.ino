#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiClientSecure.h>

const char* ssid = "Sharework Guest";
const char* password = "Sh@rew0rk";
const char* mqtt_server = "de6686b6c05c4c2985ed2a5c67d01b2c.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_topic = "Node1";
const char* mqtt_client_id = "RandD";
const char* mqtt_username = "RandD";
const char* mqtt_password = "Sharework@123";

#define RX2_PIN 16
#define TX2_PIN 17
#define BAUD_RATE 921600
#define GMT_OFFSET 7 * 3600
#define MQTT_SEND_INTERVAL 1000
#define TIMEOUT_DURATION 3000
#define CONFIDENCE_THRESHOLD 0.85

WiFiClientSecure espClient;
PubSubClient client(espClient);

String buffer = "";
String aiResult = "";
String lastValidData = "";
unsigned long lastDataTime = 0;
unsigned long lastMqttSendTime = 0;
bool hasValidData = false;

bool getLocalTimeWithTimeout(struct tm *info, uint32_t ms) {
  uint32_t start = millis();
  while ((millis() - start) < ms) {
    if (getLocalTime(info)) return true;
    delay(10);
  }
  return false;
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTimeWithTimeout(&timeinfo, 5000)) {
    unsigned long seconds = millis() / 1000;
    char buf[64];
    snprintf(buf, sizeof(buf), "ESP32-%lu", seconds);
    return String(buf);
  }
  char buf[64]; 
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void callback(char* topic, byte* payload, unsigned int length) {
  for (unsigned int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println();
}

void setup() {
  Serial.begin(921600);
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX2_PIN, TX2_PIN);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(1024);
  configTime(GMT_OFFSET, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  struct tm timeinfo;
  if (!getLocalTimeWithTimeout(&timeinfo, 5000)) {
    Serial.println("NTP failed");
  } else {
    Serial.println("NTP synced");
  }
  lastDataTime = millis();
  lastMqttSendTime = millis();
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password)) break;
    delay(5000);
  }
}

String extractValue(String data, String key) {
  int keyPos = data.indexOf(key + ":");
  if (keyPos == -1) return "";
  int start = keyPos + key.length() + 1;
  int end = data.indexOf(",", start);
  if (end == -1) end = data.length();
  return data.substring(start, end);
}

void parseFloatArray(String data, float* array, int size) {
  int start = 0;
  for (int i = 0; i < size; i++) {
    int end = data.indexOf(';', start);
    if (end == -1) end = data.length();
    if (start < data.length()) {
      array[i] = data.substring(start, end).toFloat();
    } else {
      array[i] = 0.0;
    }
    start = end + 1;
  }
}

void sendMQTTData(String dataToSend, bool isCurrentData) {
  if (!client.connected()) reconnect();
  DynamicJsonDocument doc(2048);
  if (dataToSend.length() == 0 || !isCurrentData) {
    doc["timestamp"] = getCurrentTimestamp();
    if (dataToSend.length() == 0) {
      doc["status"] = "NoData";
      doc["confident"] = 0;
    } else {
      String classStr = extractValue(dataToSend, "Class");
      String confStr = extractValue(dataToSend, "Conf");
      String probsStr = extractValue(dataToSend, "Probs");
      String rmsStr = extractValue(dataToSend, "RMS");
      String peakStr = extractValue(dataToSend, "Peak");
      String p2pStr = extractValue(dataToSend, "P2P");
      String cfStr = extractValue(dataToSend, "CF");
      String freqsStr = extractValue(dataToSend, "Freqs");
      String magsStr = extractValue(dataToSend, "Mags");
      float confidence = confStr.toFloat();
      String finalStatus = classStr;
      if (confidence < CONFIDENCE_THRESHOLD) finalStatus = "Warning";
      doc["status"] = finalStatus;
      doc["confident"] = confidence;
      JsonArray probArray = doc.createNestedArray("prob");
      float probs[4] = {0};
      parseFloatArray(probsStr, probs, 4);
      for (int i = 0; i < 4; i++) probArray.add(probs[i]);
      doc["rms"] = rmsStr.toFloat();
      doc["peak"] = peakStr.toFloat();
      doc["peak_to_peak"] = p2pStr.toFloat();
      doc["crest_factor"] = cfStr.toFloat();
      JsonArray freqArray = doc.createNestedArray("dominant_frequencies");
      float freqs[5] = {0};
      parseFloatArray(freqsStr, freqs, 5);
      for (int i = 0; i < 5; i++) freqArray.add(freqs[i]);
      JsonArray magArray = doc.createNestedArray("frequency_magnitudes");
      float mags[5] = {0};
      parseFloatArray(magsStr, mags, 5);
      for (int i = 0; i < 5; i++) magArray.add(mags[i]);
    }
    if (!doc.containsKey("prob")) {
      JsonArray probArray = doc.createNestedArray("prob");
      for (int i = 0; i < 4; i++) probArray.add(0);
    }
    if (!doc.containsKey("rms")) {
      doc["rms"] = 0;
      doc["peak"] = 0;
      doc["peak_to_peak"] = 0;
      doc["crest_factor"] = 0;
    }
    if (!doc.containsKey("dominant_frequencies")) {
      JsonArray freqArray = doc.createNestedArray("dominant_frequencies");
      for (int i = 0; i < 5; i++) freqArray.add(0);
    }
    if (!doc.containsKey("frequency_magnitudes")) {
      JsonArray magArray = doc.createNestedArray("frequency_magnitudes");
      for (int i = 0; i < 5; i++) magArray.add(0);
    }
  } else {
    String classStr = extractValue(dataToSend, "Class");
    String confStr = extractValue(dataToSend, "Conf");
    String probsStr = extractValue(dataToSend, "Probs");
    String rmsStr = extractValue(dataToSend, "RMS");
    String peakStr = extractValue(dataToSend, "Peak");
    String p2pStr = extractValue(dataToSend, "P2P");
    String cfStr = extractValue(dataToSend, "CF");
    String freqsStr = extractValue(dataToSend, "Freqs");
    String magsStr = extractValue(dataToSend, "Mags");
    float confidence = confStr.toFloat();
    String finalStatus = classStr;
    if (confidence < CONFIDENCE_THRESHOLD) finalStatus = "Warning";
    doc["timestamp"] = getCurrentTimestamp();
    doc["status"] = finalStatus;
    doc["confident"] = confidence;
    JsonArray probArray = doc.createNestedArray("prob");
    float probs[4] = {0};
    parseFloatArray(probsStr, probs, 4);
    for (int i = 0; i < 4; i++) probArray.add(probs[i]);
    doc["rms"] = rmsStr.toFloat();
    doc["peak"] = peakStr.toFloat();
    doc["peak_to_peak"] = p2pStr.toFloat();
    doc["crest_factor"] = cfStr.toFloat();
    JsonArray freqArray = doc.createNestedArray("dominant_frequencies");
    float freqs[5] = {0};
    parseFloatArray(freqsStr, freqs, 5);
    for (int i = 0; i < 5; i++) freqArray.add(freqs[i]);
    JsonArray magArray = doc.createNestedArray("frequency_magnitudes");
    float mags[5] = {0};
    parseFloatArray(magsStr, mags, 5);
    for (int i = 0; i < 5; i++) magArray.add(mags[i]);
  }
  String payload;
  serializeJson(doc, payload);
  client.publish(mqtt_topic, payload.c_str());
}

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  if (line.startsWith("Class:") && line.indexOf("Peak:") != -1 && line.indexOf("RMS:") != -1) {
    aiResult = line;
    lastValidData = line;
    hasValidData = true;
    lastDataTime = millis();
  }
}

void readSTM32Data() {
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.print(c);
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        processLine(buffer);
        buffer = "";
      }
    } else if (c >= 32 && c <= 126) {
      buffer += c;
    }
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  readSTM32Data();
  if (millis() - lastMqttSendTime >= MQTT_SEND_INTERVAL) {
    if (aiResult.length() > 0) {
      sendMQTTData(aiResult, true);
      aiResult = "";
      lastDataTime = millis();
    } else if (hasValidData && (millis() - lastDataTime < TIMEOUT_DURATION)) {
      sendMQTTData(lastValidData, false);
    } else {
      sendMQTTData("", false);
    }
    lastMqttSendTime = millis();
  }
  delay(1);
}
