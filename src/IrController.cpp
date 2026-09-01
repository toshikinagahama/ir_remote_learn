#include "IrController.h"
#include <ir_Panasonic.h>

IrController irController;

static const uint16_t kCaptureBufSize = 1024;
static const uint8_t  kRecvTimeoutMs = 50;

void IrController::begin(uint16_t recvPin, uint16_t sendPin) {
  irrecv_ = new IRrecv(recvPin, kCaptureBufSize, kRecvTimeoutMs, true);
  irsend_ = new IRsend(sendPin);
  irac_ = new IRac(sendPin);
  irrecv_->enableIRIn();
  irsend_->begin();
  loadSlots();
  IRac::initState(&acState_);
  loadAcState();
}

void IrController::resumeReceiver() {
  irrecv_->resume();
}

bool IrController::pollDecode() {
  return irrecv_->decode(&results_);
}

bool IrController::storeLastDecodeToSlot(uint8_t n) {
  if (n >= kMaxSlots) return false;
  IrSlot &s = slots[n];

  if (results_.decode_type == decode_type_t::UNKNOWN) {
    // 未知プロトコル: デコードできた既知プロトコルの情報を持たないため、
    // 受信した生のパルス列(raw波形)をそのまま保存し、送信時に再生する
    uint16_t rawLen = getCorrectedRawLength(&results_);
    if (rawLen == 0 || rawLen > kRawMaxLen) return false;
    uint16_t *rawArr = resultToRawArray(&results_);
    s.valid = true;
    s.protocol = decode_type_t::UNKNOWN;
    s.isState = false;
    s.isRaw = true;
    s.rawLen = rawLen;
    memcpy(s.raw, rawArr, rawLen * sizeof(uint16_t));
    delete[] rawArr;
    s.value = 0;
    s.bits = 0;
    s.stateBytes = 0;
    saveSlot(n);
    return true;
  }

  s.valid = true;
  s.protocol = results_.decode_type;
  s.bits = results_.bits;
  s.isState = hasACState(results_.decode_type);
  s.isRaw = false;
  s.rawLen = 0;
  if (s.isState) {
    s.stateBytes = (results_.bits + 7) / 8;
    if (s.stateBytes > kStateSizeMax) s.stateBytes = kStateSizeMax;
    memcpy(s.state, results_.state, s.stateBytes);
    s.value = 0;
  } else {
    s.value = results_.value;
    s.stateBytes = 0;
  }
  saveSlot(n);
  return true;
}

bool IrController::sendSlot(uint8_t n) {
  if (n >= kMaxSlots || !slots[n].valid) return false;
  IrSlot &s = slots[n];
  if (s.isRaw) {
    irsend_->sendRaw(s.raw, s.rawLen, 38);
    return true;
  }
  if (s.isState) {
    return irsend_->send(s.protocol, s.state, s.stateBytes);
  }
  return irsend_->send(s.protocol, s.value, s.bits);
}

void IrController::setLabel(uint8_t n, const String &label) {
  if (n >= kMaxSlots) return;
  slots[n].label = label.substring(0, kLabelMaxLen);
  saveSlot(n);
}

void IrController::setCategory(uint8_t n, uint8_t category) {
  if (n >= kMaxSlots) return;
  slots[n].category = category;
  saveSlot(n);
}

void IrController::deleteSlot(uint8_t n) {
  if (n >= kMaxSlots) return;
  uint8_t keepCategory = slots[n].category;
  slots[n] = IrSlot();
  slots[n].category = keepCategory;  // タブ分類は削除後も維持する
  saveSlot(n);
}

String IrController::slotInfoLine(uint8_t n) const {
  if (n >= kMaxSlots) return "";
  const IrSlot &s = slots[n];
  String line = "slot,";
  line += n;
  line += ",";
  line += (s.valid ? "1" : "0");
  line += ",";
  line += s.label;
  line += ",";
  line += s.valid ? (s.isRaw ? "UNKNOWN(RAW)" : typeToString(s.protocol).c_str()) : "";
  line += ",";
  line += s.bits;
  line += ",";
  line += s.category;
  return line;
}

String IrController::slotKey(uint8_t n) const {
  return "slot" + String(n);
}

void IrController::saveSlot(uint8_t n) {
  prefs_.begin("ircodes", false);
  String key = slotKey(n);
  IrSlot &s = slots[n];
  // 末尾にisRaw(1B)+rawLen(2B)を追加した新形式(17B)。旧形式(13B)のデータはloadSlots側で判別して読む
  uint8_t buf[1 + 1 + 8 + 2 + 2 + 1 + 2];
  buf[0] = (uint8_t)s.valid;
  buf[1] = (uint8_t)s.isState;
  memcpy(&buf[2], &s.value, 8);
  memcpy(&buf[10], &s.bits, 2);
  memcpy(&buf[12], &s.stateBytes, 2);
  buf[14] = (uint8_t)s.isRaw;
  memcpy(&buf[15], &s.rawLen, 2);
  prefs_.putUChar((key + "p").c_str(), (uint8_t)s.protocol);
  prefs_.putBytes(key.c_str(), buf, sizeof(buf));
  prefs_.putBytes((key + "st").c_str(), s.state, kStateSizeMax);
  if (s.isRaw && s.rawLen > 0) {
    prefs_.putBytes((key + "rw").c_str(), s.raw, s.rawLen * sizeof(uint16_t));
  }
  prefs_.putString((key + "lb").c_str(), s.label);
  prefs_.putUChar((key + "c").c_str(), s.category);
  prefs_.end();
}

// 初回起動時のみ、スロット0-4=テレビ/5-9=照明/10-15=その他 の初期タブ分けを行う
static uint8_t defaultCategoryForSlot(uint8_t n) {
  if (n < 5) return SlotCategory::TV;
  if (n < 10) return SlotCategory::LIGHT;
  return SlotCategory::OTHER;
}

void IrController::loadSlots() {
  prefs_.begin("ircodes", true);
  bool categoriesInitialized = prefs_.getBool("cat_init", false);
  for (uint8_t n = 0; n < kMaxSlots; n++) {
    String key = slotKey(n);
    uint8_t buf[1 + 1 + 8 + 2 + 2 + 1 + 2];
    size_t len = prefs_.getBytes(key.c_str(), buf, sizeof(buf));
    // 新形式(17B)・旧形式(13B、isRaw追加前に保存されたデータ)の両方を受け付ける
    if (len == sizeof(buf) || len == 13) {
      slots[n].valid = (bool)buf[0];
      slots[n].isState = (bool)buf[1];
      memcpy(&slots[n].value, &buf[2], 8);
      memcpy(&slots[n].bits, &buf[10], 2);
      memcpy(&slots[n].stateBytes, &buf[12], 2);
      if (len == sizeof(buf)) {
        slots[n].isRaw = (bool)buf[14];
        memcpy(&slots[n].rawLen, &buf[15], 2);
      } else {
        slots[n].isRaw = false;
        slots[n].rawLen = 0;
      }
      slots[n].protocol = (decode_type_t)prefs_.getUChar((key + "p").c_str(), decode_type_t::UNKNOWN);
      prefs_.getBytes((key + "st").c_str(), slots[n].state, kStateSizeMax);
      if (slots[n].isRaw && slots[n].rawLen > 0) {
        prefs_.getBytes((key + "rw").c_str(), slots[n].raw, slots[n].rawLen * sizeof(uint16_t));
      }
      slots[n].label = prefs_.getString((key + "lb").c_str(), "");
    }
    slots[n].category = prefs_.getUChar((key + "c").c_str(), defaultCategoryForSlot(n));
  }
  prefs_.end();

  if (!categoriesInitialized) {
    // 初回のみ、計算したデフォルトカテゴリを確定保存する
    for (uint8_t n = 0; n < kMaxSlots; n++) {
      saveSlot(n);
    }
    prefs_.begin("ircodes", false);
    prefs_.putBool("cat_init", true);
    prefs_.end();
  }
}

void IrController::forEachSupportedAcBrand(void (*callback)(const String &line)) const {
  for (int p = 1; p <= decode_type_t::kLastDecodeType; p++) {
    decode_type_t protocol = (decode_type_t)p;
    if (IRac::isProtocolSupported(protocol)) {
      String line = "acbrand,";
      line += (int)protocol;
      line += ",";
      line += typeToString(protocol).c_str();
      callback(line);
    }
  }
}

void IrController::acSetBrand(decode_type_t protocol) {
  bool power = acState_.power;
  stdAc::opmode_t mode = acState_.mode;
  float degrees = acState_.degrees;
  stdAc::fanspeed_t fan = acState_.fanspeed;
  stdAc::swingv_t swingv = acState_.swingv;
  IRac::initState(&acState_);
  acState_.protocol = protocol;
  acState_.power = power;
  acState_.mode = mode;
  acState_.degrees = degrees;
  acState_.fanspeed = fan;
  acState_.swingv = swingv;

  Serial.printf("acSetBrand: called with protocol=%d (%s)\n", (int)protocol, typeToString(protocol).c_str());

  // 同じプロトコルで学習済みの実機コードがあれば、そこから型番(model)を推定して引き継ぐ。
  // PANASONIC_AC等は型番(CKP/DKE/JKE/NKE/RKR/LKE等)ごとに符号化が異なり、
  // model未指定(-1=デフォルト)のまま汎用送信すると実機が反応しない/一部しか反応しないことがある
  if (protocol == decode_type_t::PANASONIC_AC) {
    bool found = false;
    for (uint8_t n = 0; n < kMaxSlots; n++) {
      Serial.printf("  slot%u: valid=%d isState=%d protocol=%d\n",
                    n, slots[n].valid, slots[n].isState, (int)slots[n].protocol);
      if (slots[n].valid && slots[n].isState && slots[n].protocol == protocol) {
        IRPanasonicAc detector(0);  // 送信はしない、デコード専用に使う
        detector.setRaw(slots[n].state);
        acState_.model = (int16_t)detector.getModel();
        Serial.printf("acSetBrand: PANASONIC_AC model %d をスロット%uの学習データから引き継ぎ\n",
                      acState_.model, n);
        found = true;
        break;
      }
    }
    if (!found) {
      Serial.println("acSetBrand: 一致する学習済みPANASONIC_ACスロットが見つからず、model=-1のまま");
    }
  }

  saveAcState();
}

void IrController::acSetModel(uint8_t modelWire) {
  if (acState_.protocol == decode_type_t::UNKNOWN) return;
  stdAc::state_t prev = acState_;
  acState_.model = (modelWire == 255) ? (int16_t)-1 : (int16_t)modelWire;
  Serial.printf("acSetModel: model=%d に変更し即送信\n", acState_.model);
  bool ok = irac_->sendAc(acState_, &prev);
  Serial.printf("acSetModel: send %s\n", ok ? "OK" : "failed");
  if (ok) saveAcState();
}

bool IrController::acApplyState(bool power, uint8_t mode, uint8_t temp, uint8_t fan, bool swing) {
  if (acState_.protocol == decode_type_t::UNKNOWN || !IRac::isProtocolSupported(acState_.protocol)) {
    return false;
  }
  static const stdAc::opmode_t kModeTable[5] = {
    stdAc::opmode_t::kAuto, stdAc::opmode_t::kCool, stdAc::opmode_t::kHeat,
    stdAc::opmode_t::kDry, stdAc::opmode_t::kFan
  };
  // sendAc()の差分検知(prev)に使うため、変更前の状態を退避してから書き換える
  stdAc::state_t prev = acState_;

  acState_.power = power;
  acState_.mode = kModeTable[mode % 5];
  acState_.degrees = (float)temp;
  acState_.fanspeed = (stdAc::fanspeed_t)(fan % 7);
  acState_.swingv = swing ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;

  bool ok = irac_->sendAc(acState_, &prev);
  if (ok) saveAcState();
  return ok;
}

static uint8_t opmodeToWire(stdAc::opmode_t mode) {
  switch (mode) {
    case stdAc::opmode_t::kCool: return 1;
    case stdAc::opmode_t::kHeat: return 2;
    case stdAc::opmode_t::kDry:  return 3;
    case stdAc::opmode_t::kFan:  return 4;
    default: return 0;  // kAuto/kOff
  }
}

String IrController::acStateLine() const {
  String line = "ac_state,";
  line += (acState_.power ? "1" : "0");
  line += ",";
  line += (int)acState_.protocol;
  line += ",";
  line += (acState_.protocol == decode_type_t::UNKNOWN) ? "" : typeToString(acState_.protocol).c_str();
  line += ",";
  line += opmodeToWire(acState_.mode);
  line += ",";
  line += (int)acState_.degrees;
  line += ",";
  line += (int)acState_.fanspeed;
  line += ",";
  line += (acState_.swingv == stdAc::swingv_t::kOff ? "0" : "1");
  line += ",";
  line += (int)acState_.model;
  return line;
}

void IrController::getAcWireState(bool &power, uint8_t &mode, uint8_t &temp, uint8_t &fan, bool &swing) const {
  power = acState_.power;
  mode = opmodeToWire(acState_.mode);
  temp = (uint8_t)acState_.degrees;
  fan = (uint8_t)acState_.fanspeed;
  swing = (acState_.swingv != stdAc::swingv_t::kOff);
}

String IrController::acShadowJson() const {
  String j = "{\"power\":";
  j += (acState_.power ? "true" : "false");
  j += ",\"mode\":";
  j += opmodeToWire(acState_.mode);
  j += ",\"temp\":";
  j += (int)acState_.degrees;
  j += ",\"fan\":";
  j += (int)acState_.fanspeed;
  j += ",\"swing\":";
  j += (acState_.swingv == stdAc::swingv_t::kOff ? "false" : "true");
  j += "}";
  return j;
}

static String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

String IrController::slotsShadowJson() const {
  String j = "[";
  bool first = true;
  for (uint8_t n = 0; n < kMaxSlots; n++) {
    const IrSlot &s = slots[n];
    if (!s.valid || s.label.length() == 0) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"slot\":";
    j += n;
    j += ",\"label\":\"";
    j += jsonEscape(s.label);
    j += "\",\"category\":";
    j += s.category;
    j += "}";
  }
  j += "]";
  return j;
}

void IrController::saveAcState() {
  prefs_.begin("ircodes", false);
  prefs_.putBytes("ac_state", &acState_, sizeof(acState_));
  prefs_.end();
}

void IrController::loadAcState() {
  prefs_.begin("ircodes", true);
  stdAc::state_t loaded;
  size_t len = prefs_.getBytes("ac_state", &loaded, sizeof(loaded));
  if (len == sizeof(loaded)) {
    acState_ = loaded;
  }
  prefs_.end();
}
