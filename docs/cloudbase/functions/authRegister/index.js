// authRegister：注册/登录合一。手机号+短信验证码（+姓名/单位，首次必填）。
// 短信验证码校验走 CloudBase 身份验证 HTTP 通道（固定「腾讯云」签名，个人账号可用，
// 无需企业短信资质）——端点细节联调时与客户端一起实锤（见 VERIFY_SMS_TODO）。
// 部署：Node.js 16+；环境变量 AUTH_SECRET；HTTP 触发器路径 /authRegister
const tcb = require('@cloudbase/node-sdk');
const crypto = require('crypto');

const SECRET = process.env.AUTH_SECRET || '';
const TOKEN_MS = 30 * 86400e3;

async function verifySmsCode(app, phone, code) {
    // VERIFY_SMS_TODO(联调)：CloudBase Auth 的短信验证码 HTTP 校验。
    // 候选实现：POST https://{env}.service.tcloudbase.com/auth/v1/verification/verify
    //   {phone_number, verification_code}（以 Web SDK 实际请求为准）
    // 联调前占位：拒绝一切（返回 false）——未接通前注册不可用是安全的默认。
    return false;
}

exports.main = async (event) => {
    let body = {};
    try { body = JSON.parse(event.body || '{}'); } catch { /* ignore */ }
    const phone = String(body.phone || '').trim();
    const code = String(body.code || '').trim();
    const name = String(body.name || '').trim();
    const org = String(body.org || '').trim();
    if (!/^1\d{10}$/.test(phone) || !code)
        return { statusCode: 400, body: JSON.stringify({ error: 'bad_request' }) };

    const app = tcb.init({ env: tcb.SYMBOL_CURRENT_ENV });
    if (!(await verifySmsCode(app, phone, code)))
        return { statusCode: 401, body: JSON.stringify({ error: 'sms_code_invalid' }) };

    const db = app.database();
    const now = Date.now();
    const exist = await db.collection('users').where({ phone }).get();
    if (exist.data && exist.data.length > 0) {
        const u = exist.data[0];
        if (u.status === 'disabled')
            return { statusCode: 403, body: JSON.stringify({ error: 'disabled' }) };
        await db.collection('users').where({ phone }).update({
            lastSeenAt: now,
            lastVersion: String(body.version || ''),
            lastPlatform: String(body.platform || ''),
        });
    } else {
        if (!name || !org)
            return { statusCode: 400, body: JSON.stringify({ error: 'name_org_required' }) };
        await db.collection('users').add({
            phone, name, org, source: 'sms', createdAt: now, lastSeenAt: now,
            lastVersion: String(body.version || ''),
            lastPlatform: String(body.platform || ''),
            status: 'active',
        });
    }

    const payload = Buffer.from(JSON.stringify({
        phone, kind: 'sms', exp: now + TOKEN_MS,
        nonce: crypto.randomBytes(8).toString('hex'),
    })).toString('base64url');
    const token = payload + '.'
        + crypto.createHmac('sha256', SECRET).update(payload).digest('hex');
    return { statusCode: 200, body: JSON.stringify({ ok: true, token, exp: now + TOKEN_MS }) };
};
