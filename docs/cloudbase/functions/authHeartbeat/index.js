// authHeartbeat：心跳/续期。验 token → 更新 users.lastSeenAt/版本 → 返回服务端时间
// 部署：Node.js 16+；环境变量 AUTH_SECRET；HTTP 触发器路径 /authHeartbeat
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

const SECRET = process.env.AUTH_SECRET || '';
const SMS_TOKEN_MS = 30 * 86400e3;      // 短信用户 30 天
const INVITE_TOKEN_MS = 180 * 86400e3;  // 邀请码用户 180 天

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

exports.main = async (event) => {
    let body = {};
    try { body = JSON.parse(event.body || '{}'); } catch { /* ignore */ }
    const claims = verifyToken(body.token);
    if (!claims)
        return { statusCode: 401, body: JSON.stringify({ error: 'invalid_or_expired_token' }) };

    const app = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV });
    const db = app.database();
    const now = Date.now();
    // 停用检查（管理端把 users.status 改为 disabled 即拒绝）
    const u = await db.collection('users').where({ phone: claims.phone }).limit(1).get().catch(() => null);
    if (u && u.data && u.data.length && (u.data[0].status === 'disabled' || u.data[0].disabled === true))
        return { statusCode: 401, body: JSON.stringify({ error: 'user_disabled' }) };
    await db.collection('users').where({ phone: claims.phone }).update({
        lastSeenAt: now,
        lastVersion: String(body.version || ''),
        lastPlatform: String(body.platform || ''),
    });

    // 距离到期不足 7 天：自动续签（30 天策略的平滑续期）
    let newToken = null;
    const remain = claims.exp - now;
    if (remain < 7 * 86400e3) {
        const ttl = claims.kind === 'invite' ? INVITE_TOKEN_MS : SMS_TOKEN_MS;
        const payload = Buffer.from(JSON.stringify({
            phone: claims.phone, kind: claims.kind || 'sms',
            exp: now + ttl, nonce: crypto.randomBytes(8).toString('hex'),
        })).toString('base64url');
        newToken = payload + '.'
            + crypto.createHmac('sha256', SECRET).update(payload).digest('hex');
    }
    return { statusCode: 200, body: JSON.stringify({ ok: true, now, token: newToken }) };
};
