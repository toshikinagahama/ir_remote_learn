'use strict';

const {
  IoTDataPlaneClient,
  GetThingShadowCommand,
  UpdateThingShadowCommand,
} = require('@aws-sdk/client-iot-data-plane');

const THING_NAME = process.env.THING_NAME || 'ir-remote-esp32-01';
// Alexa Smart Homeスキルは FE(極東)リージョンのLambdaをus-west-2にしか置けない制約があるため、
// Lambda実行リージョンとIoT Core(ap-northeast-1)のリージョンが異なる。
// IoT Data Plane操作(Get/UpdateThingShadow)はアカウント固有のATSエンドポイントが必須(汎用リージョンエンドポイント不可)。
const iotData = new IoTDataPlaneClient({
  region: process.env.IOT_REGION || 'ap-northeast-1',
  endpoint: `https://${process.env.IOT_ENDPOINT || 'a1ajhh2hgcnpxc-ats.iot.ap-northeast-1.amazonaws.com'}`,
});

// --- Shadow helpers -------------------------------------------------------

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

// --- Alexa Response helpers ------------------------------------------------

function alexaResponse(header, endpoint, payload, contextProperties) {
  const response = {
    event: {
      header: { ...header, messageId: cryptoRandomId(), payloadVersion: '3' },
      payload: payload || {},
    },
  };
  if (endpoint) response.event.endpoint = endpoint;
  if (contextProperties) response.context = { properties: contextProperties };
  return response;
}

function cryptoRandomId() {
  return 'msg-' + Date.now() + '-' + Math.random().toString(36).slice(2, 10);
}

function nowIso() {
  return new Date().toISOString();
}

// AC(サーモスタット)エンドポイントの現在値からAlexa context propertiesを組み立てる
function acContextProperties(ac) {
  const modeMap = { 0: 'AUTO', 1: 'COOL', 2: 'HEAT', 3: 'AUTO', 4: 'AUTO' }; // DRY/FANはAlexa標準に無いためAUTO扱い
  return [
    {
      namespace: 'Alexa.ThermostatController',
      name: 'targetSetpoint',
      value: { value: ac.temp != null ? ac.temp : 25, scale: 'CELSIUS' },
      timeOfSample: nowIso(),
      uncertaintyInMilliseconds: 500,
    },
    {
      namespace: 'Alexa.ThermostatController',
      name: 'thermostatMode',
      value: ac.power === false ? 'OFF' : (modeMap[ac.mode] || 'AUTO'),
      timeOfSample: nowIso(),
      uncertaintyInMilliseconds: 500,
    },
    {
      namespace: 'Alexa.PowerController',
      name: 'powerState',
      value: ac.power ? 'ON' : 'OFF',
      timeOfSample: nowIso(),
      uncertaintyInMilliseconds: 500,
    },
    {
      namespace: 'Alexa.EndpointHealth',
      name: 'connectivity',
      value: { value: 'OK' },
      timeOfSample: nowIso(),
      uncertaintyInMilliseconds: 500,
    },
  ];
}

// --- Directive handlers ------------------------------------------------

async function handleDiscovery() {
  const reported = await getReportedShadow();
  const slots = Array.isArray(reported.slots) ? reported.slots : [];

  const LIGHT_CATEGORY = 2;

  const endpoints = slots.map((s) => {
    if (s.category === LIGHT_CATEGORY) {
      // 照明は1ボタントグル式のリモコンを学習させている前提のため、
      // ON/OFFどちらの発話が来ても同じ信号(sendSlot)を送信する。
      // 実際の点灯/消灯状態はESP32側で追跡できないため retrievable: false にし、
      // 「今ついてる?」のようなReportState要求には応じない。
      return {
        endpointId: `slot-${s.slot}`,
        manufacturerName: 'ir_remote_learn',
        friendlyName: s.label,
        description: 'IR Remote Light (ir_remote_learn)',
        displayCategories: ['LIGHT'],
        capabilities: [
          { type: 'AlexaInterface', interface: 'Alexa', version: '3' },
          {
            type: 'AlexaInterface',
            interface: 'Alexa.PowerController',
            version: '3',
            properties: { supported: [{ name: 'powerState' }], retrievable: false, proactivelyReported: false },
          },
        ],
      };
    }
    return {
      endpointId: `slot-${s.slot}`,
      manufacturerName: 'ir_remote_learn',
      friendlyName: s.label,
      description: 'IR Remote Scene (ir_remote_learn)',
      displayCategories: ['SCENE_TRIGGER'],
      capabilities: [
        { type: 'AlexaInterface', interface: 'Alexa', version: '3' },
        {
          type: 'AlexaInterface',
          interface: 'Alexa.SceneController',
          version: '3',
          supportsDeactivation: false,
          proactivelyReported: false,
        },
      ],
    };
  });

  endpoints.push({
    endpointId: 'ac-1',
    manufacturerName: 'ir_remote_learn',
    friendlyName: 'エアコン',
    description: 'IR Remote AC (ir_remote_learn, IRac汎用制御)',
    displayCategories: ['THERMOSTAT'],
    capabilities: [
      { type: 'AlexaInterface', interface: 'Alexa', version: '3' },
      {
        type: 'AlexaInterface',
        interface: 'Alexa.PowerController',
        version: '3',
        properties: { supported: [{ name: 'powerState' }], retrievable: true, proactivelyReported: false },
      },
      {
        type: 'AlexaInterface',
        interface: 'Alexa.ThermostatController',
        version: '3',
        properties: {
          supported: [{ name: 'targetSetpoint' }, { name: 'thermostatMode' }],
          retrievable: true,
          proactivelyReported: false,
        },
        configuration: {
          supportedModes: ['AUTO', 'COOL', 'HEAT', 'OFF'],
          supportsScheduling: false,
        },
      },
      { type: 'AlexaInterface', interface: 'Alexa.EndpointHealth', version: '3',
        properties: { supported: [{ name: 'connectivity' }], retrievable: true, proactivelyReported: false } },
    ],
  });

  return {
    event: {
      header: {
        namespace: 'Alexa.Discovery',
        name: 'Discover.Response',
        payloadVersion: '3',
        messageId: cryptoRandomId(),
      },
      payload: { endpoints },
    },
  };
}

async function handleSceneActivate(directive) {
  const endpointId = directive.endpoint.endpointId; // "slot-<n>"
  const slot = parseInt(endpointId.replace('slot-', ''), 10);
  // "<slot>-<timestamp>"形式にする。AWS IoT Shadowのdeltaは「値そのものが変化したフィールド」
  // しか含まないため、素のslot番号だけだと同じスロットを連続送信した時にsendSlotフィールド自体が
  // deltaに現れず(値として変化なしと判定され)ESP32に届かない。値に毎回変わる要素を混ぜることで
  // 必ずdeltaに含まれるようにする(cmdIdを別フィールドに分離する案は、sendSlot自体の値が
  // 変わらない限りdeltaにsendSlotキーが出てこず失敗した)
  await updateDesired({ sendSlot: `${slot}-${Date.now()}` });

  return alexaResponse(
    { namespace: 'Alexa.SceneController', name: 'ActivationStarted' },
    directive.endpoint,
    { cause: { type: 'VOICE_INTERACTION' }, timestamp: nowIso() }
  );
}

async function handlePowerController(directive) {
  const endpointId = directive.endpoint.endpointId;

  if (endpointId.startsWith('slot-')) {
    // 照明(トグル式)。TurnOn/TurnOffどちらでも同じ信号を送るだけで、
    // retrievable: false のためcontext propertiesは返さない。
    // "<slot>-<timestamp>"形式にする理由はhandleSceneActivateのコメント参照
    const slot = parseInt(endpointId.replace('slot-', ''), 10);
    await updateDesired({ sendSlot: `${slot}-${Date.now()}` });
    return alexaResponse(
      { namespace: 'Alexa', name: 'Response' },
      directive.endpoint,
      {}
    );
  }

  const power = directive.header.name === 'TurnOn';
  // cmdIdは常に変化する値を混ぜることで、前回と同じ状態(例: 既にON中にもう一度ON)を
  // 指定した場合でもAWS IoT Shadowのdeltaが確実に生成されるようにするnonce
  // (詳細はESP32側 AwsIotClient::processDelta のコメント参照)。
  // 数値のままだとArduinoJson側で13桁の整数を精度落ちなく往復させるのが面倒なため文字列で送る
  await updateDesired({ power, cmdId: String(Date.now()) });
  const reported = await getReportedShadow();
  const ac = { ...(reported.ac || reported), power };
  return alexaResponse(
    { namespace: 'Alexa', name: 'Response' },
    directive.endpoint,
    {},
    acContextProperties(ac)
  );
}

async function handleThermostatController(directive) {
  const name = directive.header.name;
  const reported = await getReportedShadow();
  const currentAc = reported.ac || reported || {};
  const desired = {};

  if (name === 'SetTargetTemperature') {
    const temp = Math.round(directive.payload.targetSetpoint.value);
    desired.temp = Math.max(16, Math.min(30, temp));
  } else if (name === 'AdjustTargetTemperature') {
    const delta = Math.round(directive.payload.targetSetpointDelta.value);
    const base = currentAc.temp != null ? currentAc.temp : 25;
    desired.temp = Math.max(16, Math.min(30, base + delta));
  } else if (name === 'SetThermostatMode') {
    const mode = directive.payload.thermostatMode.value;
    if (mode === 'OFF') {
      desired.power = false;
    } else {
      desired.power = true;
      const modeToWire = { AUTO: 0, COOL: 1, HEAT: 2 };
      desired.mode = modeToWire[mode] != null ? modeToWire[mode] : 0;
    }
  }

  // cmdIdの理由はhandlePowerControllerのコメント参照
  desired.cmdId = String(Date.now());
  await updateDesired(desired);
  const mergedAc = { ...currentAc, ...desired };

  return alexaResponse(
    { namespace: 'Alexa', name: 'Response' },
    directive.endpoint,
    {},
    acContextProperties(mergedAc)
  );
}

async function handleReportState(directive) {
  const reported = await getReportedShadow();
  const ac = reported.ac || reported || {};
  return alexaResponse(
    { namespace: 'Alexa', name: 'StateReport' },
    directive.endpoint,
    {},
    acContextProperties(ac)
  );
}

// --- Entry point ------------------------------------------------

exports.handler = async (event) => {
  const directive = event.directive;
  const namespace = directive.header.namespace;
  const name = directive.header.name;

  try {
    if (namespace === 'Alexa.Discovery' && name === 'Discover') {
      return await handleDiscovery();
    }
    if (namespace === 'Alexa.SceneController' && name === 'Activate') {
      return await handleSceneActivate(directive);
    }
    if (namespace === 'Alexa.PowerController') {
      return await handlePowerController(directive);
    }
    if (namespace === 'Alexa.ThermostatController') {
      return await handleThermostatController(directive);
    }
    if (namespace === 'Alexa' && name === 'ReportState') {
      return await handleReportState(directive);
    }

    return alexaResponse(
      { namespace: 'Alexa', name: 'ErrorResponse' },
      directive.endpoint,
      { type: 'INVALID_DIRECTIVE', message: `Unsupported directive: ${namespace}.${name}` }
    );
  } catch (err) {
    console.error(err);
    return alexaResponse(
      { namespace: 'Alexa', name: 'ErrorResponse' },
      directive.endpoint,
      { type: 'INTERNAL_ERROR', message: String(err && err.message || err) }
    );
  }
};
