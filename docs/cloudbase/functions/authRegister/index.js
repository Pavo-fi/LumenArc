// authRegister —— 手机号用户注册/登录（短信验证码由客户端直连 CloudBase 身份验证完成，
// 本函数收到客户端登录成功后的 CloudBase access_token，调 user/me 验证并取出手机号）。
// POST {access_token, name, org} -> {token, expires_at}
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

const ENV_ID = 'lumenarc-prod-d6gcdfb6a8873d906';
const TOKEN_TTL_SMS_MS = 30 * 24 * 3600 * 1000;   // 短信用户 30 天

function signToken(phone, kind, expMs, secret) {
    const payload = Buffer.from(JSON.stringify({
        phone, kind, exp: expMs, nonce: crypto.randomBytes(8).toString('hex'),
    })).toString('base64url');
    const sig = crypto.createHmac('sha256', secret).update(payload).digest('hex');
    return `${payload}.${sig}`;
}

async function fetchPhone(accessToken) {
    const r = await fetch(`https://${ENV_ID}.api.tcloudbasegateway.com/auth/v1/user/me?client_id=${ENV_ID}`, {
        headers: { 'Authorization': `Bearer ${accessToken}` }
    });
    if (!r.ok) return null;
    const j = await r.json().catch(() => null);
    const p = j && (j.phone_number || j.phone);
    return p ? String(p) : null;
}

exports.main = async (event) => {
    const body = typeof event.body === 'string' ? JSON.parse(event.body || '{}') : (event.body || event);
    const { access_token, name, org } = body || {};
    if (!access_token || !name || !org) return { statusCode: 400, body: JSON.stringify({ error: 'missing_fields' }) };

    const phone = await fetchPhone(access_token).catch(() => null);
    if (!phone) return { statusCode: 401, body: JSON.stringify({ error: 'cloudbase_token_invalid' }) };

    const app = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV });
    const db = app.database();
    const users = db.collection('users');
    const now = Date.now();
    const exist = await users.where({ phone }).limit(1).get().catch(() => null);
    if (exist && exist.data && exist.data.length) {
        await users.doc(exist.data[0]._id).update({ lastLoginAt: now, name, org }).catch(() => {});
    } else {
        await users.add({ phone, name, org, createdAt: now, lastLoginAt: now, disabled: false }).catch(() => {});
    }
    const exp = now + TOKEN_TTL_SMS_MS;
    const token = signToken(phone, 'sms', exp, process.env.AUTH_SECRET || '');
    return { statusCode: 200, body: JSON.stringify({ token, expires_at: exp }) };
};
