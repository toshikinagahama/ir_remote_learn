#ifndef IRCONTROLLER_H
#define IRCONTROLLER_H

#include <Arduino.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>
#include <Preferences.h>

const uint8_t kMaxSlots = 16;
// BLEデフォルトMTU(23B)内でSET_LABELが1回のwriteValueで収まるよう短めに制限
// (cmd 2B + slot 1B + label ≤ 20B ATTペイロード上限)
const uint8_t kLabelMaxLen = 17;
const uint32_t kLearnTimeoutMs = 15000;

// スロットのカテゴリ(タブ分け用)
namespace SlotCategory {
  constexpr uint8_t OTHER = 0;
  constexpr uint8_t TV    = 1;
  constexpr uint8_t LIGHT = 2;
}

struct IrSlot {
  bool valid = false;
  bool isState = false;  // true: エアコン等の状態型プロトコル(state配列使用)
  decode_type_t protocol = decode_type_t::UNKNOWN;
  uint64_t value = 0;
  uint16_t bits = 0;
  uint8_t state[kStateSizeMax] = {0};
  uint16_t stateBytes = 0;
  String label = "";
  uint8_t category = SlotCategory::OTHER;
};

/**
 * @brief IR受信/送信/スロット永続化(NVS)を一手に扱うコントローラ
 * BLE層・State層はこのクラス経由でのみIRハードウェアへアクセスする
 */
class IrController {
public:
  IrSlot slots[kMaxSlots];

  void begin(uint16_t recvPin, uint16_t sendPin);

  // 学習(受信)用 — StateLearningの専用タスクから毎ループ呼ばれる想定
  void resumeReceiver();
  bool pollDecode();                       // 1回分のdecode試行。受信できればtrue
  bool isLastDecodeUnknown() const;
  void storeLastDecodeToSlot(uint8_t n);   // 直近decode結果をスロットnへ保存(NVS永続化含む)

  // 送信
  bool sendSlot(uint8_t n);

  // ラベル/スロット管理
  void setLabel(uint8_t n, const String &label);
  void setCategory(uint8_t n, uint8_t category);
  void deleteSlot(uint8_t n);

  // BLE通知用の1行テキスト "slot,<n>,<valid>,<label>,<protocol>,<bits>,<category>"
  String slotInfoLine(uint8_t n) const;

  // --- 汎用エアコン(IRac)制御 ---
  // ライブラリが対応する全ブランドを "acbrand,<id>,<name>" 形式で1件ずつcallbackへ渡す
  void forEachSupportedAcBrand(void (*callback)(const String &line)) const;
  void acSetBrand(decode_type_t protocol);
  // mode: 0=auto 1=cool 2=heat 3=dry 4=fan / fan: stdAc::fanspeed_t の値をそのまま / swing: 0=off 1=auto
  bool acApplyState(bool power, uint8_t mode, uint8_t temp, uint8_t fan, bool swing);
  String acStateLine() const;  // "ac_state,<power>,<protocol_id>,<protocol_name>,<mode>,<temp>,<fan>,<swing>"

private:
  IRrecv *irrecv_ = nullptr;
  IRsend *irsend_ = nullptr;
  IRac *irac_ = nullptr;
  decode_results results_;
  Preferences prefs_;
  stdAc::state_t acState_;

  void saveSlot(uint8_t n);
  void loadSlots();
  String slotKey(uint8_t n) const;
  void saveAcState();
  void loadAcState();
};

extern IrController irController;

#endif // IRCONTROLLER_H
