#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @brief WiFi認証情報の永続化(NVS)・接続管理
 * BLE(web/remote.html)経由でSSID/PWを設定し、AWS IoT接続用のネットワーク基盤を提供する
 */
class WifiManager {
public:
  void begin();  // NVSから読み込み、保存済み認証情報があれば自動接続を試みる

  void setSsid(const String &ssid);
  void setPassword(const String &password);
  String getSsid() const { return ssid_; }

  void connect();  // 非同期でWiFi.begin()を呼ぶ(ブロックしない)
  bool isConnected() const;

  // "wifi_status,<connected 0/1>,<ssid>,<ip>"
  String statusLine() const;

private:
  String ssid_;
  String password_;
  Preferences prefs_;

  void save();
  void load();
};

extern WifiManager wifiManager;

#endif // WIFIMANAGER_H
