#include "WifiManager.h"
#include <WiFi.h>

WifiManager wifiManager;

void WifiManager::begin() {
  load();
  if (ssid_.length() > 0) {
    connect();
  }
}

void WifiManager::setSsid(const String &ssid) {
  ssid_ = ssid;
  save();
}

void WifiManager::setPassword(const String &password) {
  password_ = password;
  save();
}

void WifiManager::connect() {
  if (ssid_.length() == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_.c_str(), password_.c_str());
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::statusLine() const {
  String line = "wifi_status,";
  line += isConnected() ? "1" : "0";
  line += ",";
  line += ssid_;
  line += ",";
  line += isConnected() ? WiFi.localIP().toString() : "";
  return line;
}

void WifiManager::save() {
  prefs_.begin("wifi", false);
  prefs_.putString("ssid", ssid_);
  prefs_.putString("pw", password_);
  prefs_.end();
}

void WifiManager::load() {
  prefs_.begin("wifi", true);
  ssid_ = prefs_.getString("ssid", "");
  password_ = prefs_.getString("pw", "");
  prefs_.end();
}
