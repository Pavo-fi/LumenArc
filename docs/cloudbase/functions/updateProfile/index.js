// updateProfile：账号管理-修改姓名/单位。双重校验：
//   1) 自家 token 验签（claims.phone）
//   2) access_token 调 user/me 证明当前持有该手机号（且与 token 手机号一致）
// POST {token, access_token, name, org} -> {ok}
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

const ENV_ID = 'lumenarc-prod-d6gcdfb6a8873d906';
const SECRET = process.env.AUTH_SECRET || '';

function verifyToken(token) {
    if (!token || !SECRET) return null;
    const dot = token.lastIndexOf('.');
    if (dot <= 0) return null;
    const payload = token.slice(0, dot);
    const sig = token.slice(dot + 1);
    const expect = crypto.createHmac('sha256', SECRET).update(payload).digest('hex');
    if (sig.length !== expect.length
        || !crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(expect)))
        return null;
    let obj;
    try { obj = JSON.parse(Buffer.from(payload, 'base64url').toString()); }
    catch { return null; }
    if (!obj || !obj.phone || typeof obj.exp !== 'number' || Date.now() > obj.exp)
        return null;
    return obj;
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
    let body = {};
    try { body = JSON.parse(event.body || '{}'); } catch { /* ignore */ }
    const claims = verifyToken(body.token);
    if (!claims) return { statusCode: 401, body: JSON.stringify({ error: 'invalid_or_expired_token' }) };
    if (String(claims.phone).startsWith('invite:'))
        return { statusCode: 400, body: JSON.stringify({ error: 'invite_cannot_edit' }) };

    const name = String(body.name || '').trim();
    const org = String(body.org || '').trim();
    if (!name || !org) return { statusCode: 400, body: JSON.stringify({ error: 'missing_fields' }) };

    const phone = await fetchPhone(String(body.access_token || '')).catch(() => null);
    if (!phone) return { statusCode: 401, body: JSON.stringify({ error: 'cloudbase_token_invalid' }) };
    if (phone !== claims.phone)
        return { statusCode: 403, body: JSON.stringify({ error: 'phone_mismatch' }) };

    const db = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV }).database();
    await db.collection('users').where({ phone: claims.phone }).update({ name, org });
    return { statusCode: 200, body: JSON.stringify({ ok: true }) };
};
