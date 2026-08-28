// feedback：意见反馈。验 token → 写 feedback 表（诊断附件走小体积 JSON 摘要；
// 大附件后续可接 COS 预签名上传，一期不装）。
// 部署：Node.js 16+；环境变量 AUTH_SECRET；HTTP 触发器路径 /feedback
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

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

exports.main = async (event) => {
    let body = {};
    try { body = JSON.parse(event.body || '{}'); } catch { /* ignore */ }
    const claims = verifyToken(body.token);
    if (!claims)
        return { statusCode: 401, body: JSON.stringify({ error: 'invalid_or_expired_token' }) };

    const text = String(body.text || '').trim();
    if (!text)
        return { statusCode: 400, body: JSON.stringify({ error: 'empty_text' }) };

    const app = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV });
    const db = app.database();
    let name = String(body.name || '');
    if (!name) {
        const u = await db.collection('users').where({ phone: claims.phone }).limit(1).get().catch(() => null);
        if (u && u.data && u.data.length) name = String(u.data[0].name || '');
    }
    await db.collection('feedback').add({
        phone: claims.phone,
        name,
        version: String(body.version || ''),
        platform: String(body.platform || ''),
        text: text.slice(0, 4000),
        diag: body.diag && typeof body.diag === 'object' ? body.diag : null,  // 客户端保证无案件数据
        status: 'new',
        createdAt: Date.now(),
    });
    return { statusCode: 200, body: JSON.stringify({ ok: true }) };
};
