# Alexa音声操作 セットアップ手順（ゼロから）

`ir_remote_learn`のESP32を、AWS IoT Core + Lambda + Cognito + Alexa Smart Homeスキル経由でAlexaから音声操作できるようにするまでの手順。実際に一からハマった箇所も含めて記載しているので、上から順にやれば再現できるはず。

## 前提条件

- AWSアカウント（無料枠で足りる規模）
- Amazon開発者アカウント（Alexaスキル用、AWSアカウントと同じでもよい）
- ローカル環境: `aws` CLI（設定済み・認証済み）、Node.js 18以上、npm
- ESP32-DEVKITC-VIE実機、`ir_remote_learn`のBLE版ファームウェアが書き込み・動作確認済みであること

以降、以下の値を自分の環境に読み替える:

| プレースホルダ | 意味 | この手順内の例 |
|---|---|---|
| `<AWS_ACCOUNT_ID>` | AWSアカウントID | `453393474681` |
| `<IOT_REGION>` | IoT Coreを置くリージョン | `ap-northeast-1`（東京） |
| `<THING_NAME>` | IoT Thing名 | `ir-remote-esp32-01` |

---

## 1. AWS IoT Core: Thing・証明書・ポリシーを作る

```bash
# Thing作成
aws iot create-thing --thing-name <THING_NAME> --region <IOT_REGION>

# 証明書+鍵ペア発行（ローカルにファイル出力される。private keyは絶対に他人に渡さない・gitに入れない）
mkdir -p /tmp/iot_certs && cd /tmp/iot_certs
aws iot create-keys-and-certificate \
  --set-as-active \
  --certificate-pem-outfile device-cert.pem.crt \
  --public-key-outfile device-public.pem.key \
  --private-key-outfile device-private.pem.key \
  --region <IOT_REGION> > cert_result.json

# certificateArnを控える
CERT_ARN=$(python3 -c "import json; print(json.load(open('cert_result.json'))['certificateArn'])")
echo "$CERT_ARN"
```

### IoTポリシー（このThingのshadowトピックのみに権限を絞る）

```bash
cat > policy.json << EOF
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:<IOT_REGION>:<AWS_ACCOUNT_ID>:client/<THING_NAME>"
    },
    {
      "Effect": "Allow",
      "Action": ["iot:Publish", "iot:Receive"],
      "Resource": ["arn:aws:iot:<IOT_REGION>:<AWS_ACCOUNT_ID>:topic/\$aws/things/<THING_NAME>/shadow/*"]
    },
    {
      "Effect": "Allow",
      "Action": "iot:Subscribe",
      "Resource": ["arn:aws:iot:<IOT_REGION>:<AWS_ACCOUNT_ID>:topicfilter/\$aws/things/<THING_NAME>/shadow/*"]
    }
  ]
}
EOF

aws iot create-policy --policy-name <THING_NAME>-policy --policy-document file://policy.json --region <IOT_REGION>
aws iot attach-policy --policy-name <THING_NAME>-policy --target "$CERT_ARN" --region <IOT_REGION>
aws iot attach-thing-principal --thing-name <THING_NAME> --principal "$CERT_ARN" --region <IOT_REGION>

# 接続先エンドポイント（ATS、Data Plane用）を取得
aws iot describe-endpoint --endpoint-type iot:Data-ATS --region <IOT_REGION>
```

### Root CA証明書を取得

```bash
curl -o /tmp/iot_certs/AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

これで`/tmp/iot_certs/`に3つのPEMファイル(`device-cert.pem.crt` / `device-private.pem.key` / `AmazonRootCA1.pem`)とエンドポイントURLが揃う。**この後の手順6で使うので消さずに残しておく**。

---

## 2. AWS Lambda: Alexaスキルのバックエンド

### 重要: リージョンはus-west-2固定

Alexa Smart Homeスキルは、Alexaサービス側がリクエストを送るリージョン(NA/EU/FE)ごとに、Lambdaを置ける場所が決まっている。**日本(FEリージョン)向けスキルのLambdaは`us-west-2`にしか置けない**。IoT Core自体は東京(`ap-northeast-1`)のままでよく、LambdaのSDKクライアント側でリージョンを明示指定してクロスリージョンで呼び出す。

### コード

`aws/lambda/index.js`と`aws/lambda/package.json`は本リポジトリに同梱済み。中身は:
- `Alexa.Discovery.Discover`: shadowの`reported.slots`（ラベル付き学習スロット）を`SceneController`エンドポイントとして返す。加えてエアコン用に`ThermostatController`+`PowerController`を持つ固定エンドポイント`ac-1`
- `Alexa.SceneController.Activate` → shadow desiredに`{sendSlot: n}`
- `Alexa.ThermostatController.*` / `Alexa.PowerController.*` → shadow desiredに`{power, mode, temp, fan, swing}`
- `Alexa.ReportState` → shadowのreportedを返す

`index.js`冒頭のIoT Data Plane接続先は自分の環境に合わせて書き換える(エンドポイント・リージョンは環境変数`IOT_ENDPOINT`/`IOT_REGION`でも上書き可能、デフォルト値をコード内で変えてもよい)。

### IAMロール作成

```bash
cat > trust-policy.json << 'EOF'
{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Principal": {"Service": "lambda.amazonaws.com"}, "Action": "sts:AssumeRole"}]
}
EOF

aws iam create-role --role-name <THING_NAME>-lambda-role --assume-role-policy-document file://trust-policy.json

aws iam attach-role-policy \
  --role-name <THING_NAME>-lambda-role \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole

cat > iot-shadow-policy.json << EOF
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": ["iot:GetThingShadow", "iot:UpdateThingShadow"],
    "Resource": "arn:aws:iot:<IOT_REGION>:<AWS_ACCOUNT_ID>:thing/<THING_NAME>"
  }]
}
EOF

aws iam put-role-policy \
  --role-name <THING_NAME>-lambda-role \
  --policy-name iot-shadow-access \
  --policy-document file://iot-shadow-policy.json
```

IAMロール作成直後はAWS内部での伝播に数秒〜十数秒かかることがある。次のLambda作成でロールが見つからないエラーが出たら少し待って再実行する。

### デプロイ

```bash
cd aws/lambda
npm install --production
zip -r function.zip index.js node_modules package.json

aws lambda create-function \
  --function-name <THING_NAME>-alexa-skill \
  --runtime nodejs20.x \
  --role arn:aws:iam::<AWS_ACCOUNT_ID>:role/<THING_NAME>-lambda-role \
  --handler index.handler \
  --zip-file fileb://function.zip \
  --timeout 10 \
  --memory-size 128 \
  --environment "Variables={THING_NAME=<THING_NAME>,IOT_REGION=<IOT_REGION>,IOT_ENDPOINT=<手順1で取得したエンドポイント>}" \
  --region us-west-2
```

コード更新時は`aws lambda update-function-code --function-name <THING_NAME>-alexa-skill --zip-file fileb://function.zip --region us-west-2`。

---

## 3. Amazon Cognito: アカウントリンク用OAuth2プロバイダ

Alexa Smart Homeスキルはアカウントリンク(OAuth2)が必須。自前でOAuthサーバーを書く代わりにCognito User Poolを使う。

```bash
# User Pool作成
aws cognito-idp create-user-pool --pool-name <THING_NAME>-users --region <IOT_REGION>
# 出力の UserPool.Id を控える (例: ap-northeast-1_XXXXXXXXX)

# ホストUIドメイン作成（ドメイン名は全世界で一意、アカウントID等を混ぜて衝突回避）
aws cognito-idp create-user-pool-domain \
  --domain <THING_NAME>-<AWS_ACCOUNT_ID> \
  --user-pool-id <User Pool Id> \
  --region <IOT_REGION>

# App Client作成（コールバックURLは後で手順5終了後に正しい値へ更新する。今は仮のURLでよい）
aws cognito-idp create-user-pool-client \
  --user-pool-id <User Pool Id> \
  --client-name alexa-skill-client \
  --generate-secret \
  --allowed-o-auth-flows code \
  --allowed-o-auth-scopes openid \
  --allowed-o-auth-flows-user-pool-client \
  --supported-identity-providers COGNITO \
  --callback-urls "https://example.com/placeholder" \
  --explicit-auth-flows ALLOW_USER_PASSWORD_AUTH ALLOW_REFRESH_TOKEN_AUTH \
  --region <IOT_REGION>
# 出力の ClientId と ClientSecret を控える(ClientSecretは再表示不可なので必ずこの時点でメモする)
```

### ログイン用ユーザー作成（個人利用前提、1人分）

```bash
aws cognito-idp admin-create-user \
  --user-pool-id <User Pool Id> \
  --username <自分のメールアドレス> \
  --user-attributes Name=email,Value=<自分のメールアドレス> Name=email_verified,Value=true \
  --message-action SUPPRESS \
  --region <IOT_REGION>

aws cognito-idp admin-set-user-password \
  --user-pool-id <User Pool Id> \
  --username <自分のメールアドレス> \
  --password "<強めのパスワードを自分で決める、記号/大小英字/数字混在>" \
  --permanent \
  --region <IOT_REGION>
```

このユーザー名・パスワードが、後でAlexaアプリのアカウントリンク画面で入力するログイン情報になる。

---

## 4. ASK CLI インストール・認証

```bash
npm install -g ask-cli
ask configure --no-browser
```

表示されたURLをブラウザで開いてAmazon開発者アカウントでログイン→認証コードが表示される→**そのままターミナルの`Please enter the Authorization Code:`プロンプトに直接タイプ/ペーストしてEnter**（チャットツール等の別経路で貼っても意味が無い、待機中のプロンプトへ直接入力する必要がある）。

`ask smapi list-vendors`等が通れば認証成功。表示される`vendorId`を控える（手順5で使う）。

---

## 5. Alexaスキル作成

### スキルマニフェスト

`aws/skill-manifest.json`を自分の情報で編集（`vendorId`・Lambda ARN）:

```json
{
  "vendorId": "<手順4で控えたvendorId>",
  "manifest": {
    "publishingInformation": {
      "locales": {
        "ja-JP": {
          "name": "IRリモコン",
          "summary": "ESP32 IR学習リモコンをAlexaから操作",
          "description": "...",
          "examplePhrases": ["アレクサ、テレビをつけて", "アレクサ、エアコンを26度にして"],
          "keywords": ["赤外線", "リモコン"]
        }
      },
      "isAvailableWorldwide": false,
      "distributionCountries": ["JP"],
      "category": "SMART_HOME"
    },
    "apis": {
      "smartHome": {
        "endpoint": { "uri": "arn:aws:lambda:us-west-2:<AWS_ACCOUNT_ID>:function:<THING_NAME>-alexa-skill" }
      }
    },
    "manifestVersion": "1.0"
  }
}
```

**ハマりどころ**: SMAPIのリクエストボディのキーは`manifest`であって`skillManifest`ではない。間違えると`400 UNEXPECTED_PROPERTY`が返る。

### Lambda呼び出し権限（先に広めの権限で仮許可 → スキル作成後に絞り込む）

スキルIDは作成してみないと分からないので、**最初は広めの権限で仮許可**しておかないと、スキル作成時のバリデーションで「Lambdaの呼び出し権限が無い」と弾かれて**スキル自体が作成失敗（ロールバック）**する、というニワトリタマゴ問題がある。

```bash
aws lambda add-permission \
  --function-name <THING_NAME>-alexa-skill \
  --statement-id alexa-smarthome-trigger \
  --action lambda:InvokeFunction \
  --principal alexa-connectedhome.amazon.com \
  --region us-west-2
```

### スキル作成

```bash
ask smapi create-skill-for-vendor --manifest "file://aws/skill-manifest.json"
# 出力の skillId を控える (amzn1.ask.skill.xxxxxxxx 形式)

# 数秒待ってからステータス確認、SUCCEEDEDになっていればOK
ask smapi get-skill-status --skill-id <skillId> --resource manifest
```

`FAILED`になった場合、`get-skill-status`のエラーメッセージを読んで原因を直し、**もう一度`create-skill-for-vendor`からやり直す**（`update-skill-manifest`で直そうとしても、バリデーション失敗したスキルはそもそも実体が作られておらず`404 Skill not found`になる。修正して再作成が確実）。

### Lambda権限をこのスキルIDに絞り込む（仮許可を本許可に変更）

```bash
aws lambda remove-permission --function-name <THING_NAME>-alexa-skill --statement-id alexa-smarthome-trigger --region us-west-2

aws lambda add-permission \
  --function-name <THING_NAME>-alexa-skill \
  --statement-id alexa-smarthome-trigger \
  --action lambda:InvokeFunction \
  --principal alexa-connectedhome.amazon.com \
  --event-source-token <skillId> \
  --region us-west-2
```

### アカウントリンク設定

```bash
cat > account-linking.json << EOF
{
  "accountLinkingRequest": {
    "type": "AUTH_CODE",
    "authorizationUrl": "https://<THING_NAME>-<AWS_ACCOUNT_ID>.auth.<IOT_REGION>.amazoncognito.com/oauth2/authorize",
    "accessTokenUrl": "https://<THING_NAME>-<AWS_ACCOUNT_ID>.auth.<IOT_REGION>.amazoncognito.com/oauth2/token",
    "clientId": "<手順3のClientId>",
    "clientSecret": "<手順3のClientSecret>",
    "clientAuthenticationScheme": "HTTP_BASIC",
    "scopes": ["openid"],
    "domains": []
  }
}
EOF

ask smapi update-account-linking-info --skill-id <skillId> --account-linking-request "file://account-linking.json"
```

**ハマりどころ**: リクエストボディは`accountLinkingRequest`でラップする必要がある（中身だけ渡すと`400 Parsing error due to empty body`という分かりにくいエラーになる）。

`account-linking.json`は実際のclientSecretを含むため**gitにコミットしない**こと（本リポジトリの`aws/account-linking.json`はプレースホルダ版のテンプレートなので、それを見ながら別ファイルで作業する）。

---

## 6. Alexa側のリダイレクトURLをCognitoに登録

1. https://developer.amazon.com/alexa/console/ask を開き、作成したスキルを選択
2. `Build`タブ → `ACCOUNT LINKING`セクションを開く
3. ページ下部に表示される「Alexa Redirect URLs」を3つとも控える（`https://alexa.amazon.co.jp/api/skill/link/<vendorId>` 等、末尾にvendorIdが入る）

```bash
aws cognito-idp update-user-pool-client \
  --user-pool-id <User Pool Id> \
  --client-id <手順3のClientId> \
  --client-name alexa-skill-client \
  --allowed-o-auth-flows code \
  --allowed-o-auth-scopes openid \
  --allowed-o-auth-flows-user-pool-client \
  --supported-identity-providers COGNITO \
  --callback-urls "<URL1>" "<URL2>" "<URL3>" \
  --explicit-auth-flows ALLOW_USER_PASSWORD_AUTH ALLOW_REFRESH_TOKEN_AUTH \
  --region <IOT_REGION>
```

---

## 7. ESP32ファームウェア側の設定

### secrets.h を作る

```bash
cp include/secrets.h.example include/secrets.h
```

`include/secrets.h`を開き、手順1で取得した3つのPEM(`AmazonRootCA1.pem` / `device-cert.pem.crt` / `device-private.pem.key`)の中身をそれぞれ該当箇所に貼り付け、`AWS_IOT_ENDPOINT`と`AWS_IOT_THING_NAME`も書き換える。**このファイルは`.gitignore`済みなので、間違ってコミットされる心配は無い（ただし念のため`git status`で確認する癖をつけること）**。

### ビルド・書き込み

```bash
pio run -t upload
```

### WiFi設定・クラウドモードへの切替

1. `web/remote.html`をChromeで開き、BLE接続
2. 下部の「WiFi設定」パネルでSSID/パスワードを入力し「保存して接続」
3. 同パネルの「クラウドモードへ切替（BLE切断）」を押す（**確認ダイアログが出る、元に戻すには本体の電源再投入が必要**）
4. シリアルモニタで`AWS IoT: MQTT connected`→`subscribed to shadow delta/get-accepted`が出れば接続成功

---

## 8. Alexaアプリでの最終設定

1. Alexaアプリ → その他 → スキル・ゲーム → 開発中のスキットで自分のスキルを探して有効化
2. アカウントリンク画面が出る → 手順3で作ったCognitoユーザー(メールアドレス/パスワード)でログイン
3. 「デバイスを検出」を実行 → 学習済みラベル付きスロットとエアコン(`ac-1`)が見つかる
4. 「アレクサ、[ラベル名]をつけて」「アレクサ、エアコンを26度にして」「アレクサ、エアコンを消して」で操作

---

## トラブルシューティング

### `SSL - Memory allocation failed` / `X509 - Allocation of memory failed`

BLE+WiFi+TLSを同時稼働させるヒープが足りない。以下がファームウェアに実装済みのはずだが、確認する:

- `setup()`冒頭で`esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)`をBLE初期化より前に呼んでいるか
- `web/remote.html`の「クラウドモードへ切替」を押してBLEを止めてから接続しているか（BLE稼働中はAWS IoT接続を試みない設計になっている）
- それでも足りない場合、AWS IoTタスクのスタックサイズ(`main.cpp`の`xTaskCreatePinnedToCore(awsIotTaskFunc, ...)`、現在16KB)を増やしてみる

### 間欠的に`PK - pubkey tag or value is invalid`等の不可解なTLSエラーが出る

FreeRTOSタスクのスタック不足によるスタックオーバーフローの疑い。TLSハンドシェイク(RSA)はスタック消費が大きいので、該当タスクのスタックサイズを増やす。

### `X509 - Certificate verification failed`

NTP時刻同期ができていない可能性が高い（ESP32はRTC電池が無くリセットで1970年に戻る）。シリアルログに`AWS IoT: NTP time sync started`が出てから数秒〜十数秒待つ。それでも解決しない場合はWiFi経由でNTPサーバー(`pool.ntp.org`)に到達できているか確認する。

### shadowのdeltaが処理された形跡がシリアルログに全く出ない

- そもそもクラウドモードに切り替えていない（BLEモードのままではAWS IoT接続を試みない）
- MQTT自体は繋がっているが`shadow/get`のリクエストが飛んでいない → ファームウェアが古い可能性、`pio run -t upload`し直す

### deltaは受信できているログが出るのに、実機のIR送信や状態変化が起きない

これが一番ハマりやすかったポイント。BLE切断イベントで内部のState machineが`STATE_ADVERTISE`に戻ってしまい、AC制御コマンドを処理するはずの`StateIdle`のハンドラに届かず**黙って無視される**バグが過去にあった（`d6ef7a8`で修正済み）。最新のfirmwareを使っているか確認する。`AwsIotClient`が`IrController`を直接呼ぶ実装(State machineを経由しない)になっていればこの問題は起きない。

### Panasonic ACなど特定機種で電源offだけ効かない

エアコンブランドによっては型番(Panasonicなら CKP/DKE/JKE/LKE/NKE/RKR)の指定が必要な場合がある。`web/remote.html`のエアコンタブ下部「型番(詳細設定)」から手動で試す。学習済みスロットがあれば、ブランド再選択時に自動で型番を推定して引き継ぐ。

### `Skill not found` (404) が`update-skill-manifest`等で出る

スキル作成(`create-skill-for-vendor`)がバリデーションエラーで失敗した場合、スキルの実体が作られず、そのskillIdに対する後続操作は全て404になる。エラー内容を直してから**もう一度`create-skill-for-vendor`で作り直す**（Lambda呼び出し権限が無い等が典型的な原因、手順5の「先に広めの権限で仮許可」を参照）。

---

## 参考: Alexaの温度モードの制約

Alexa標準の`ThermostatController`は`AUTO`/`COOL`/`HEAT`/`OFF`のみサポートし、除湿(DRY)・送風(FAN)は無い。音声では上記4つのみ操作可能で、除湿・送風は`web/remote.html`側からのみ操作する設計になっている。
