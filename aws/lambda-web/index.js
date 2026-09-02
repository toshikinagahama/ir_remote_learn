'use strict';

const {
  IoTDataPlaneClient,
  GetThingShadowCommand,
  UpdateThingShadowCommand,
} = require('@aws-sdk/client-iot-data-plane');

const THING_NAME = process.env.THING_NAME || 'ir-remote-esp32-01';
const WEB_API_PASSWORD = process.env.WEB_API_PASSWORD;

const iotData = new IoTDataPlaneClient({
  region: process.env.IOT_REGION || 'ap-northeast-1',
  endpoint: `https://${process.env.IOT_ENDPOINT}`,
});

// CORSヘッダーはLambda Function URL自体のCORS設定(AllowOrigins:["*"]等)が自動付与するため、
// ここで独自に付けると二重になり"Access-Control-Allow-Origin: *, http://..."のような
// 複数値の不正なヘッダーになりブラウザに拒否される。Content-Typeのみ返す
function respond(statusCode, body) {
  return {
    statusCode,
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  };
}

async function getReportedShadow() {
  try {
    const res = await iotData.send(new GetThingShadowCommand({ thingName: THING_NAME }));
    const body = JSON.parse(Buffer.from(res.payload).toString('utf-8'));
    return body.state && body.state.reported ? body.state.reported : {};
  } catch (e) {
    // シャドウ未作成(デバイス未接続で一度もreportedを送っていない)場合は空扱い
    return {};
  }
}

async function updateDesired(partial) {
  const payload = JSON.stringify({ state: { desired: partial } });
  await iotData.send(new UpdateThingShadowCommand({
    thingName: THING_NAME,
    payload: Buffer.from(payload),
  }));
}

exports.handler = async (event) => {
  const method = event.requestContext && event.requestContext.http
    ? event.requestContext.http.method
    : (event.httpMethod || 'POST');

  if (method === 'OPTIONS') {
    return respond(200, {});
  }

  let body;
  try {
    body = JSON.parse(event.body || '{}');
  } catch (e) {
    return respond(400, { error: 'invalid json' });
  }

  if (!WEB_API_PASSWORD || body.password !== WEB_API_PASSWORD) {
    return respond(401, { error: 'unauthorized' });
  }

  try {
    if (body.action === 'get_state') {
      const reported = await getReportedShadow();
      return respond(200, {
        slots: Array.isArray(reported.slots) ? reported.slots : [],
        ac: {
          power: !!reported.power,
          mode: reported.mode || 0,
          temp: reported.temp != null ? reported.temp : 25,
          fan: reported.fan || 0,
          swing: !!reported.swing,
        },
      });
    }

    if (body.action === 'send_slot') {
      const slot = parseInt(body.slot, 10);
      if (!Number.isInteger(slot) || slot < 0 || slot > 15) {
        return respond(400, { error: 'invalid slot' });
      }
      // AWS IoT Shadowのdeltaは値そのものが変化したフィールドしか配信しないため、
      // 同じスロットを連続で送っても確実に届くよう毎回一意な値(timestamp)を混ぜる
      // (詳細はREADME.md「AWS IoT Shadowのdelta設計」節、src/AwsIotClient.cppのコメント参照)
      await updateDesired({ sendSlot: `${slot}-${Date.now()}` });
      return respond(200, { ok: true });
    }

    if (body.action === 'set_ac') {
      const desired = { cmdId: String(Date.now()) };
      if (body.power !== undefined) desired.power = !!body.power;
      if (body.mode !== undefined) desired.mode = Number(body.mode);
      if (body.temp !== undefined) desired.temp = Math.max(16, Math.min(30, Number(body.temp)));
      if (body.fan !== undefined) desired.fan = Number(body.fan);
      if (body.swing !== undefined) desired.swing = !!body.swing;
      await updateDesired(desired);
      return respond(200, { ok: true });
    }

    return respond(400, { error: 'unknown action' });
  } catch (err) {
    console.error(err);
    return respond(500, { error: String(err && err.message || err) });
  }
};
