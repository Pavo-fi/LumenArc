// inviteActivate：离线机器邀请码通道。验 code（未使用）→ 登记 users → 发 180 天 token。
// 部署：Node.js 16+；环境变量 AUTH_SECRET；HTTP 触发器路径 /inviteActivate
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

const SECRET = process.env.AUTH_SECRET || '';
const TOKEN_MS = 180 * 86400e3;

exports.main = async (event) => {
    let body = {};
    try { body = JSON.parse(event.body || '{}'); } catch { /* ignore */ }
    const code = String(body.code || '').trim().toUpperCase();
    const name = String(body.name || '').trim();
    const org = String(body.org || '').trim();
    if (!code || !name || !org)
        return { statusCode: 400, body: JSON.stringify({ error: 'bad_request' }) };

    const app = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV });
    const db = app.database();
    const now = Date.now();

    const hit = await db.collection('invites').where({ code }).get();
    if (!hit.data || hit.data.length === 0)
        return { statusCode: 401, body: JSON.stringify({ error: 'invite_invalid' }) };
    const inv = hit.data[0];
    if (inv.usedBy)
        return { statusCode: 409, body: JSON.stringify({ error: 'invite_used' }) };

    // 邀请码用户以 code 为身份锚（离线机器可能无手机号）
    const identity = 'invite:' + code;
    await db.collection('invites').where({ code }).update({
        usedBy: name + '/' + org, usedAt: now,
    });
    const exist = await db.collection('users').where({ phone: identity }).get();
    if (!exist.data || exist.data.length === 0) {
        await db.collection('users').add({
            phone: identity, name, org, source: 'invite',
            createdAt: now, lastSeenAt: now,
            lastVersion: String(body.version || ''),
            lastPlatform: String(body.platform || ''),
            status: 'active',
        });
    } else {
        await db.collection('users').where({ phone: identity }).update({
            lastSeenAt: now, lastVersion: String(body.version || ''),
            lastPlatform: String(body.platform || ''),
        });
    }

    const payload = Buffer.from(JSON.stringify({
        phone: identity, kind: 'invite', exp: now + TOKEN_MS,
        nonce: crypto.randomBytes(8).toString('hex'),
    })).toString('base64url');
    const token = payload + '.'
        + crypto.createHmac('sha256', SECRET).update(payload).digest('hex');
    return { statusCode: 200, body: JSON.stringify({ ok: true, token, exp: now + TOKEN_MS }) };
};
