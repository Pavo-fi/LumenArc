#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LumenArc 账号服务（通道 A：邮箱自助注册 / 通道 B：邀请码·批次码）
零外部依赖：http.server + sqlite3 + smtplib + hashlib + hmac（Python 3.9+）

设计要点
- 通道 A：邮箱验证码注册。发码与注册分开计数：
    * 发码限流：同邮箱 120 秒一次；同 IP 每天 10 封；全局每天 300 封（163 配额保护）
    * 注册配额：每天最多 N 个「真的注册成功」的邮箱账号（默认 40，可配），
      刷「发码」烧不掉这个额度
- 通道 B：邀请码/批次码。不限每日配额；码带 maxUses / expiresAt / batchName
- 登录：账号（邮箱或手机号）+ 密码；token HMAC-SHA256，180 天，心跳续期
- 密码：PBKDF2-HMAC-SHA256 26 万轮 + 每用户随机 salt，绝不明文
- 迁移：老用户本地有 CloudBase token 也无效了（签名密钥不同）→ 统一用批次码重新激活

环境变量（/etc/lumenarc-auth/env）
  LA_SECRET         token 签名密钥（必填，随机 64 hex）
  LA_ADMIN_KEY      管理接口密钥（必填）
  LA_DB             默认 /var/lib/lumenarc-auth/auth.db
  LA_SMTP_HOST      smtp.163.com
  LA_SMTP_PORT      465
  LA_SMTP_USER      发信邮箱完整地址
  LA_SMTP_PASS      163 授权码
  LA_MAIL_DRYRUN    1 = 不发信，写 /var/log/lumenarc-auth/mail.log（联调用）
  LA_DAILY_EMAIL_REG  每日邮箱注册配额，默认 40
  LA_LISTEN         默认 0.0.0.0:8443
  LA_TLS_CERT/LA_TLS_KEY  给了就用 TLS（自签或正式证书）
"""

import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import smtplib
import sqlite3
import ssl
import sys
import threading
import time
from email.header import Header
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.utils import formataddr, formatdate, make_msgid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CFG = {
    "secret": os.environ.get("LA_SECRET", ""),
    "admin": os.environ.get("LA_ADMIN_KEY", ""),
    "db": os.environ.get("LA_DB", "/var/lib/lumenarc-auth/auth.db"),
    "smtp_host": os.environ.get("LA_SMTP_HOST", "smtp.163.com"),
    "smtp_port": int(os.environ.get("LA_SMTP_PORT", "465")),
    "smtp_user": os.environ.get("LA_SMTP_USER", ""),
    "smtp_pass": os.environ.get("LA_SMTP_PASS", ""),
    "dryrun": os.environ.get("LA_MAIL_DRYRUN", "0") == "1",
    "daily_email_reg": int(os.environ.get("LA_DAILY_EMAIL_REG", "40")),
    "listen": os.environ.get("LA_LISTEN", "0.0.0.0:8443"),
    "tls_cert": os.environ.get("LA_TLS_CERT", ""),
    "tls_key": os.environ.get("LA_TLS_KEY", ""),
}

TOKEN_TTL_MS = 180 * 24 * 3600 * 1000      # 180 天
CODE_TTL_MS = 5 * 60 * 1000                # 验证码 5 分钟
CODE_RESEND_MS = 120 * 1000                # 同邮箱 120 秒
IP_DAILY_MAIL = 10                         # 同 IP 每天发码上限
GLOBAL_DAILY_MAIL = 300                    # 全局每天发码上限
PBKDF2_ROUNDS = 260000

EMAIL_RE = re.compile(r"^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$")
PHONE_RE = re.compile(r"^1[3-9]\d{9}$")

_lock = threading.Lock()
_db_path = CFG["db"]


# ---------------------------------------------------------------- 基础工具
def now_ms() -> int:
    return int(time.time() * 1000)


def b64u(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def b64u_dec(s: str) -> bytes:
    pad = "=" * (-len(s) % 4)
    return base64.urlsafe_b64decode(s + pad)


def sign_token(sub: str, kind: str = "user") -> tuple:
    exp = now_ms() + TOKEN_TTL_MS
    payload = json.dumps({"sub": sub, "kind": kind, "exp": exp, "iat": now_ms()},
                         separators=(",", ":"), sort_keys=True).encode()
    body = b64u(payload)
    sig = hmac.new(CFG["secret"].encode(), body.encode(), hashlib.sha256).hexdigest()
    return f"{body}.{sig}", exp


def verify_token(token: str) -> dict:
    try:
        body, sig = token.split(".", 1)
    except ValueError:
        raise ValueError("bad_token")
    want = hmac.new(CFG["secret"].encode(), body.encode(), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(sig, want):
        raise ValueError("bad_signature")
    data = json.loads(b64u_dec(body))
    if int(data.get("exp", 0)) < now_ms():
        raise ValueError("token_expired")
    return data


def hash_password(pwd: str, salt: bytes = None) -> str:
    salt = salt or secrets.token_bytes(16)
    dk = hashlib.pbkdf2_hmac("sha256", pwd.encode(), salt, PBKDF2_ROUNDS)
    return f"pbkdf2${PBKDF2_ROUNDS}${salt.hex()}${dk.hex()}"


def check_password(pwd: str, stored: str) -> bool:
    try:
        _, rounds, salt_hex, dk_hex = stored.split("$")
        dk = hashlib.pbkdf2_hmac("sha256", pwd.encode(), bytes.fromhex(salt_hex), int(rounds))
        return hmac.compare_digest(dk.hex(), dk_hex)
    except Exception:
        return False


def pwd_ok(pwd: str, account: str) -> bool:
    if len(pwd) < 8:
        return False
    if pwd == account or pwd.lower() == account.lower():
        return False
    if pwd.isdigit():
        return False
    return True


# ---------------------------------------------------------------- 存储
def db() -> sqlite3.Connection:
    conn = sqlite3.connect(_db_path, timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_db():
    os.makedirs(os.path.dirname(_db_path), exist_ok=True)
    with db() as c:
        c.executescript(
            """
            CREATE TABLE IF NOT EXISTS users(
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              account TEXT UNIQUE NOT NULL,     -- 邮箱或手机号（统一小写）
              email TEXT,
              phone TEXT,
              name TEXT, org TEXT,
              source TEXT,                       -- email / invite
              status TEXT DEFAULT 'active',      -- active / disabled
              password_hash TEXT,
              createdAt INTEGER, lastSeenAt INTEGER, lastVersion TEXT, lastPlatform TEXT
            );
            CREATE TABLE IF NOT EXISTS codes(
              email TEXT PRIMARY KEY, code TEXT, expiresAt INTEGER, sentAt INTEGER, tries INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS mail_log(
              id INTEGER PRIMARY KEY AUTOINCREMENT, email TEXT, ip TEXT, ts INTEGER
            );
            CREATE TABLE IF NOT EXISTS inviters(
              code TEXT PRIMARY KEY, batchName TEXT, maxUses INTEGER DEFAULT 0,
              uses INTEGER DEFAULT 0, expiresAt INTEGER, createdAt INTEGER, createdBy TEXT
            );
            CREATE TABLE IF NOT EXISTS feedback(
              id INTEGER PRIMARY KEY AUTOINCREMENT, account TEXT, text TEXT,
              diag TEXT, createdAt INTEGER
            );
            CREATE TABLE IF NOT EXISTS login_log(
              id INTEGER PRIMARY KEY AUTOINCREMENT, account TEXT, ip TEXT,
              ok INTEGER, reason TEXT, ts INTEGER
            );
            CREATE INDEX IF NOT EXISTS idx_mail_ts ON mail_log(ts);
            CREATE INDEX IF NOT EXISTS idx_users_created ON users(createdAt);
            """
        )
        # 登录防爆破：老库平滑加列（失败计数 / 锁定截止）
        cols = {r["name"] for r in c.execute("PRAGMA table_info(users)").fetchall()}
        if "failCount" not in cols:
            c.execute("ALTER TABLE users ADD COLUMN failCount INTEGER DEFAULT 0")
        if "lockUntil" not in cols:
            c.execute("ALTER TABLE users ADD COLUMN lockUntil INTEGER DEFAULT 0")


def today_start_ms() -> int:
    lt = time.localtime()
    start = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, 0, 0, 0, 0, 0, -1))
    return int(start * 1000)


def log_login(c, account: str, ip: str, ok: bool, reason: str) -> None:
    """登录审计：谁、从哪、成没成、为什么。保留 90 天。"""
    t = now_ms()
    c.execute("INSERT INTO login_log(account,ip,ok,reason,ts) VALUES(?,?,?,?,?)",
              (account, ip, 1 if ok else 0, reason, t))
    if secrets.randbelow(50) == 0:
        c.execute("DELETE FROM login_log WHERE ts < ?", (t - 90 * 86400 * 1000,))


# ---------------------------------------------------------------- 发信
MAIL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mail_templates")

_TXT_FALLBACK = (
    "你的 LumenArc 验证码\n\n    {{CODE}}\n\n"
    "验证码 {{TTL_MIN}} 分钟内有效，仅可使用一次。\n\n"
    "如果这不是你本人的操作，忽略本邮件即可。\n\n"
    "—— 追光者 LumenArc\n"
)


def _tpl(name: str, fallback: str) -> str:
    """读模板；模板缺失时退回内置文本，服务不因缺文件而挂掉。"""
    try:
        with open(os.path.join(MAIL_DIR, name), "r", encoding="utf-8") as f:
            return f.read()
    except OSError:
        return fallback


def send_mail(to_addr: str, code: str) -> None:
    ttl_min = CODE_TTL_MS // 60000
    subject = f"【LumenArc】验证码 {code}，{ttl_min} 分钟内有效"
    text_body = _tpl("verify_code.txt", _TXT_FALLBACK)
    html_body = _tpl("verify_code.html", "")
    text_body = text_body.replace("{{CODE}}", code).replace("{{TTL_MIN}}", str(ttl_min))
    html_body = html_body.replace("{{CODE}}", code).replace("{{TTL_MIN}}", str(ttl_min))
    if CFG["dryrun"]:
        path = os.environ.get("LA_MAIL_LOG", "/tmp/lumenarc-mail.log")
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%F %T')}\t{to_addr}\t{code}\n")
        return
    msg = MIMEMultipart("alternative")
    msg["Subject"] = Header(subject, "utf-8")
    msg["From"] = formataddr(("追光者 LumenArc", CFG["smtp_user"]))
    msg["To"] = to_addr
    msg["Date"] = formatdate(localtime=True)
    msg["Message-ID"] = make_msgid(domain="163.com")
    msg["Auto-Submitted"] = "auto-generated"
    msg["X-Auto-Response-Suppress"] = "OOF"
    msg.attach(MIMEText(text_body, "plain", "utf-8"))
    if html_body.strip():
        msg.attach(MIMEText(html_body, "html", "utf-8"))
    ctx = ssl.create_default_context()
    with smtplib.SMTP_SSL(CFG["smtp_host"], CFG["smtp_port"], timeout=20, context=ctx) as s:
        s.login(CFG["smtp_user"], CFG["smtp_pass"])
        s.sendmail(CFG["smtp_user"], [to_addr], msg.as_string())


# ---------------------------------------------------------------- 业务
def client_ip(h) -> str:
    fwd = h.headers.get("X-Forwarded-For", "")
    return (fwd.split(",")[0].strip() if fwd else h.client_address[0]) or "?"


def api_send_code(body: dict, ip: str) -> dict:
    email = (body.get("email") or "").strip().lower()
    if not EMAIL_RE.match(email):
        return {"ok": False, "error": "bad_email", "message": "邮箱格式不对"}
    t = now_ms()
    with _lock, db() as c:
        row = c.execute("SELECT sentAt FROM codes WHERE email=?", (email,)).fetchone()
        if row and t - row["sentAt"] < CODE_RESEND_MS:
            wait = (CODE_RESEND_MS - (t - row["sentAt"])) // 1000
            return {"ok": False, "error": "too_frequent", "message": f"请 {wait} 秒后再试"}
        ip_cnt = c.execute("SELECT COUNT(*) n FROM mail_log WHERE ip=? AND ts>=?",
                           (ip, today_start_ms())).fetchone()["n"]
        if ip_cnt >= IP_DAILY_MAIL:
            return {"ok": False, "error": "ip_quota", "message": "今天这台设备发码次数已用完，请明天再试或联系管理员"}
        all_cnt = c.execute("SELECT COUNT(*) n FROM mail_log WHERE ts>=?",
                            (today_start_ms(),)).fetchone()["n"]
        if all_cnt >= GLOBAL_DAILY_MAIL:
            return {"ok": False, "error": "global_quota", "message": "今日发码总量已满，请改用邀请码注册"}
        code = f"{secrets.randbelow(1000000):06d}"
        c.execute("INSERT INTO codes(email,code,expiresAt,sentAt,tries) VALUES(?,?,?,?,0) "
                  "ON CONFLICT(email) DO UPDATE SET code=?,expiresAt=?,sentAt=?,tries=0",
                  (email, code, t + CODE_TTL_MS, t, code, t + CODE_TTL_MS, t))
        c.execute("INSERT INTO mail_log(email,ip,ts) VALUES(?,?,?)", (email, ip, t))
    try:
        send_mail(email, code)
    except Exception as e:
        return {"ok": False, "error": "smtp_failed", "message": f"发信失败：{e}"}
    return {"ok": True, "message": "验证码已发送，5 分钟内有效"}


def api_register_email(body: dict, ip: str) -> dict:
    email = (body.get("email") or "").strip().lower()
    code = (body.get("code") or "").strip()
    name = (body.get("name") or "").strip()
    org = (body.get("org") or "").strip()
    pwd = body.get("password") or ""
    if not EMAIL_RE.match(email):
        return {"ok": False, "error": "bad_email", "message": "邮箱格式不对"}
    if not pwd_ok(pwd, email):
        return {"ok": False, "error": "weak_password",
                "message": "密码至少 8 位，且不能是纯数字或等于邮箱"}
    t = now_ms()
    with _lock, db() as c:
        row = c.execute("SELECT * FROM codes WHERE email=?", (email,)).fetchone()
        if not row or row["expiresAt"] < t:
            return {"ok": False, "error": "code_expired", "message": "验证码已过期，请重新获取"}
        if row["tries"] >= 5:
            return {"ok": False, "error": "too_many_tries", "message": "验证码错误次数过多，请重新获取"}
        if not hmac.compare_digest(row["code"], code):
            c.execute("UPDATE codes SET tries=tries+1 WHERE email=?", (email,))
            return {"ok": False, "error": "code_invalid", "message": "验证码不对"}
        # 每日「成功注册」配额（只算邮箱通道）
        used = c.execute("SELECT COUNT(*) n FROM users WHERE source='email' AND createdAt>=?",
                         (today_start_ms(),)).fetchone()["n"]
        if used >= CFG["daily_email_reg"]:
            return {"ok": False, "error": "daily_quota",
                    "message": "今天自助注册名额已满，请联系管理员获取邀请码"}
        if c.execute("SELECT 1 FROM users WHERE account=?", (email,)).fetchone():
            return {"ok": False, "error": "exists", "message": "该邮箱已注册，请直接登录"}
        c.execute("DELETE FROM codes WHERE email=?", (email,))
        c.execute("INSERT INTO users(account,email,name,org,source,status,password_hash,createdAt,lastSeenAt)"
                  " VALUES(?,?,?,?,'email','active',?,?,?)",
                  (email, email, name, org, hash_password(pwd), t, t))
        token, exp = sign_token(email)
    return {"ok": True, "token": token, "expiresAt": exp, "account": email,
            "name": name, "org": org, "message": "注册成功"}


def api_register_invite(body: dict, ip: str) -> dict:
    code = (body.get("code") or "").strip()
    account = (body.get("account") or body.get("phone") or "").strip().lower()
    name = (body.get("name") or "").strip()
    org = (body.get("org") or "").strip()
    pwd = body.get("password") or ""
    if not account:
        return {"ok": False, "error": "bad_account", "message": "请填写手机号"}
    if not (PHONE_RE.match(account) or EMAIL_RE.match(account)):
        return {"ok": False, "error": "bad_account", "message": "手机号或邮箱格式不对"}
    if not pwd_ok(pwd, account):
        return {"ok": False, "error": "weak_password",
                "message": "密码至少 8 位，且不能是纯数字或等于手机号"}
    t = now_ms()
    with _lock, db() as c:
        inv = c.execute("SELECT * FROM inviters WHERE code=?", (code,)).fetchone()
        if not inv:
            return {"ok": False, "error": "invite_missing", "message": "邀请码不存在"}
        if inv["expiresAt"] and inv["expiresAt"] < t:
            return {"ok": False, "error": "invite_expired", "message": "邀请码已过期"}
        if inv["maxUses"] and inv["uses"] >= inv["maxUses"]:
            return {"ok": False, "error": "invite_used_up", "message": "邀请码名额已用完"}
        if c.execute("SELECT 1 FROM users WHERE account=?", (account,)).fetchone():
            return {"ok": False, "error": "exists", "message": "该账号已注册，请直接登录"}
        c.execute("UPDATE inviters SET uses=uses+1 WHERE code=?", (code,))
        c.execute("INSERT INTO users(account,email,phone,name,org,source,status,password_hash,createdAt,lastSeenAt)"
                  " VALUES(?,?,?,?,?,'invite','active',?,?,?)",
                  (account, account if "@" in account else None,
                   account if PHONE_RE.match(account) else None,
                   name, org, hash_password(pwd), t, t))
        token, exp = sign_token(account)
    return {"ok": True, "token": token, "expiresAt": exp, "account": account,
            "name": name, "org": org, "message": "激活成功"}


def api_login(body: dict, ip: str) -> dict:
    account = (body.get("account") or "").strip().lower()
    pwd = body.get("password") or ""
    if not account or not pwd:
        return {"ok": False, "error": "bad_request", "message": "请输入账号和密码"}
    with _lock, db() as c:
        u = c.execute("SELECT * FROM users WHERE account=?", (account,)).fetchone()
        # 反账号枚举：账号不存在 / 未设密码 / 密码错，对外一律同一句话；
        # 账号不存在时也跑一次哈希，避免用响应时间区分。
        if not u or not u["password_hash"]:
            check_password(pwd, "pbkdf2$%d$00$00" % PBKDF2_ROUNDS)
            log_login(c, account, ip, False, "no_such_account")
            # 文案与「密码错」完全一致，否则又成了账号是否存在 的探测器
            return {"ok": False, "error": "bad_credentials",
                    "message": "账号或密码不对"}
        if u["status"] != "active":
            log_login(c, account, ip, False, "disabled")
            return {"ok": False, "error": "disabled", "message": "账号已停用，请联系管理员"}
        if int(u["lockUntil"] or 0) > now_ms():
            left = (int(u["lockUntil"]) - now_ms()) // 60000 + 1
            log_login(c, account, ip, False, "locked")
            return {"ok": False, "error": "locked",
                    "message": f"密码错误次数过多，请 {left} 分钟后再试"}
        if not check_password(pwd, u["password_hash"]):
            n = int(u["failCount"] or 0) + 1
            if n >= 5:
                c.execute("UPDATE users SET failCount=0, lockUntil=? WHERE account=?",
                          (now_ms() + 15 * 60 * 1000, account))
            else:
                c.execute("UPDATE users SET failCount=? WHERE account=?", (n, account))
            log_login(c, account, ip, False, "bad_password")
            return {"ok": False, "error": "bad_credentials", "message": "账号或密码不对"}
        c.execute("UPDATE users SET lastSeenAt=?, failCount=0, lockUntil=0 WHERE account=?",
                  (now_ms(), account))
        log_login(c, account, ip, True, "ok")
        token, exp = sign_token(account)
        return {"ok": True, "token": token, "expiresAt": exp, "account": account,
                "name": u["name"] or "", "org": u["org"] or ""}


def api_set_password(body: dict, ip: str) -> dict:
    token = body.get("token") or ""
    pwd = body.get("password") or ""
    try:
        data = verify_token(token)
    except Exception as e:
        return {"ok": False, "error": "token_invalid", "message": str(e)}
    account = data["sub"]
    if not pwd_ok(pwd, account):
        return {"ok": False, "error": "weak_password",
                "message": "密码至少 8 位，且不能是纯数字或等于账号"}
    with _lock, db() as c:
        c.execute("UPDATE users SET password_hash=? WHERE account=?",
                  (hash_password(pwd), account))
    return {"ok": True, "message": "密码已设置"}


def api_heartbeat(body: dict, ip: str) -> dict:
    token = body.get("token") or ""
    try:
        data = verify_token(token)
    except Exception:
        return {"ok": False, "error": "token_invalid", "message": "登录已过期，请重新登录"}
    account = data["sub"]
    with _lock, db() as c:
        u = c.execute("SELECT status FROM users WHERE account=?", (account,)).fetchone()
        # 停用/删除的账号：心跳即失效，不必等 token 自然过期
        if not u or u["status"] != "active":
            return {"ok": False, "error": "disabled",
                    "message": "账号已停用，请联系管理员"}
        c.execute("UPDATE users SET lastSeenAt=?, lastVersion=?, lastPlatform=? WHERE account=?",
                  (now_ms(), body.get("version") or "", body.get("platform") or "", account))
    new_token, exp = sign_token(account)   # 每次心跳顺手续期
    return {"ok": True, "token": new_token, "expiresAt": exp}


def api_admin_invite(body: dict) -> dict:
    name = (body.get("batchName") or "批次").strip()
    uses = int(body.get("maxUses") or 0)
    days = int(body.get("days") or 30)
    code = "LA-" + "".join(secrets.choice("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") for _ in range(4)) \
           + "-" + "".join(secrets.choice("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") for _ in range(4))
    t = now_ms()
    with _lock, db() as c:
        c.execute("INSERT INTO inviters(code,batchName,maxUses,uses,expiresAt,createdAt,createdBy)"
                  " VALUES(?,?,?,0,?,?,?)",
                  (code, name, uses, t + days * 86400 * 1000, t, "admin"))
    return {"ok": True, "code": code, "batchName": name, "maxUses": uses,
            "expiresAt": t + days * 86400 * 1000}


def api_update_profile(body: dict, ip: str) -> dict:
    """改姓名/单位：用当前密码验明身份（不再依赖短信）。"""
    token = body.get("token") or ""
    pwd = body.get("password") or ""
    name = (body.get("name") or "").strip()
    org = (body.get("org") or "").strip()
    try:
        data = verify_token(token)
    except Exception:
        return {"ok": False, "error": "token_invalid", "message": "登录已过期，请重新登录"}
    account = data["sub"]
    with _lock, db() as c:
        u = c.execute("SELECT * FROM users WHERE account=?", (account,)).fetchone()
        if not u:
            return {"ok": False, "error": "not_found", "message": "账号不存在"}
        if not u["password_hash"] or not check_password(pwd, u["password_hash"]):
            return {"ok": False, "error": "bad_password", "message": "密码不对"}
        c.execute("UPDATE users SET name=?, org=? WHERE account=?", (name, org, account))
    return {"ok": True, "message": "资料已更新", "name": name, "org": org}


def api_feedback(body: dict, ip: str) -> dict:
    token = body.get("token") or ""
    try:
        data = verify_token(token)
    except Exception:
        return {"ok": False, "error": "token_invalid", "message": "登录已过期，请重新登录"}
    text = (body.get("text") or "").strip()
    if not text:
        return {"ok": False, "error": "empty", "message": "内容不能为空"}
    with _lock, db() as c:
        c.execute("INSERT INTO feedback(account,text,diag,createdAt) VALUES(?,?,?,?)",
                  (data["sub"], text, json.dumps(body.get("diag") or {}, ensure_ascii=False), now_ms()))
    return {"ok": True, "message": "反馈已提交，谢谢"}


def api_admin_reset(body: dict) -> dict:
    """管理员重置密码（用户忘密码时的唯一出口；密码只进 hash，不落明文）。"""
    account = (body.get("account") or "").strip().lower()
    pwd = body.get("password") or ""
    if not account or len(pwd) < 8:
        return {"ok": False, "error": "bad_request", "message": "账号不合法或新密码不足 8 位"}
    with _lock, db() as c:
        if not c.execute("SELECT 1 FROM users WHERE account=?", (account,)).fetchone():
            return {"ok": False, "error": "not_found", "message": "账号不存在"}
        c.execute("UPDATE users SET password_hash=?, failCount=0, lockUntil=0 WHERE account=?",
                  (hash_password(pwd), account))
    return {"ok": True, "message": "密码已重置"}


def api_admin_disable(body: dict) -> dict:
    """停用/恢复账号（停用后心跳立即失效，客户端被踢回登录页）。"""
    account = (body.get("account") or "").strip().lower()
    disabled = bool(body.get("disabled", True))
    with _lock, db() as c:
        if not c.execute("SELECT 1 FROM users WHERE account=?", (account,)).fetchone():
            return {"ok": False, "error": "not_found", "message": "账号不存在"}
        c.execute("UPDATE users SET status=? WHERE account=?",
                  ("disabled" if disabled else "active", account))
    return {"ok": True, "message": ("已停用 " if disabled else "已恢复 ") + account}


def api_admin_logins(limit: int = 50) -> dict:
    with db() as c:
        rows = c.execute(
            "SELECT account,ip,ok,reason,ts FROM login_log ORDER BY ts DESC LIMIT ?",
            (max(1, min(limit, 500)),)).fetchall()
    return {"ok": True, "logins": [dict(r) for r in rows]}


def api_admin_stats() -> dict:
    with db() as c:
        total = c.execute("SELECT COUNT(*) n FROM users").fetchone()["n"]
        active = c.execute("SELECT COUNT(*) n FROM users WHERE status='active'").fetchone()["n"]
        today = c.execute("SELECT COUNT(*) n FROM users WHERE createdAt>=?",
                          (today_start_ms(),)).fetchone()["n"]
        recent = c.execute("SELECT COUNT(*) n FROM users WHERE lastSeenAt>=?",
                           (now_ms() - 30 * 86400 * 1000,)).fetchone()["n"]
        mail_today = c.execute("SELECT COUNT(*) n FROM mail_log WHERE ts>=?",
                               (today_start_ms(),)).fetchone()["n"]
        inv = c.execute("SELECT code,batchName,maxUses,uses,expiresAt FROM inviters ORDER BY createdAt DESC LIMIT 10").fetchall()
    return {"ok": True, "users": {"total": total, "active": active, "today": today,
                                  "recent30d": recent},
            "mailToday": mail_today, "dailyEmailQuota": CFG["daily_email_reg"],
            "inviters": [dict(r) for r in inv]}


# ---------------------------------------------------------------- HTTP
ROUTES = {
    "/auth/send-code": ("POST", api_send_code),
    "/auth/register-email": ("POST", api_register_email),
    "/auth/register-invite": ("POST", api_register_invite),
    "/auth/login": ("POST", api_login),
    "/auth/set-password": ("POST", api_set_password),
    "/auth/update-profile": ("POST", api_update_profile),
    "/auth/heartbeat": ("POST", api_heartbeat),
    "/feedback": ("POST", api_feedback),
}


class Handler(BaseHTTPRequestHandler):
    server_version = "LumenArcAuth/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%F %T"), fmt % args))

    def _json(self, code: int, obj: dict):
        raw = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path in ("/", ""):
            return self._json(200, {"service": "lumenarc-auth",
                                    "status": "running",
                                    "mail": "dryrun" if CFG["dryrun"] else "smtp",
                                    "hint": "客户端请调用 /auth/*；健康检查 /healthz"})
        if self.path.startswith("/healthz"):
            return self._json(200, {"ok": True, "service": "lumenarc-auth",
                                    "mail": "dryrun" if CFG["dryrun"] else "smtp"})
        if self.path.startswith("/admin/stats"):
            if not self._admin_ok():
                return self._json(403, {"ok": False, "error": "forbidden"})
            return self._json(200, api_admin_stats())
        if self.path.startswith("/admin/logins"):
            if not self._admin_ok():
                return self._json(403, {"ok": False, "error": "forbidden"})
            limit = 50
            if "limit=" in self.path:
                try:
                    limit = int(self.path.split("limit=", 1)[1].split("&")[0])
                except ValueError:
                    pass
            return self._json(200, api_admin_logins(limit))
        return self._json(404, {"ok": False, "error": "not_found"})

    def _admin_ok(self) -> bool:
        key = self.headers.get("X-Admin-Key", "")
        return bool(CFG["admin"]) and hmac.compare_digest(key, CFG["admin"])

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            return self._json(400, {"ok": False, "error": "bad_json", "message": "请求格式错误"})
        ip = client_ip(self)
        if self.path.startswith("/admin/"):
            if not self._admin_ok():
                return self._json(403, {"ok": False, "error": "forbidden"})
            if self.path.startswith("/admin/invite"):
                return self._json(200, api_admin_invite(body))
            if self.path.startswith("/admin/reset-password"):
                return self._json(200, api_admin_reset(body))
            if self.path.startswith("/admin/disable"):
                return self._json(200, api_admin_disable(body))
            return self._json(404, {"ok": False, "error": "not_found"})
        route = ROUTES.get(self.path.split("?")[0])
        if not route:
            return self._json(404, {"ok": False, "error": "not_found"})
        try:
            return self._json(200, route[1](body, ip))
        except Exception as e:
            sys.stderr.write(f"ERR {self.path}: {e}\n")
            return self._json(500, {"ok": False, "error": "server_error", "message": str(e)})


def main():
    if not CFG["secret"] or not CFG["admin"]:
        print("缺少 LA_SECRET / LA_ADMIN_KEY", file=sys.stderr)
        sys.exit(2)
    init_db()
    host, _, port = CFG["listen"].rpartition(":")
    srv = ThreadingHTTPServer((host or "0.0.0.0", int(port)), Handler)
    if CFG["tls_cert"] and CFG["tls_key"]:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(CFG["tls_cert"], CFG["tls_key"])
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"lumenarc-auth listening on {CFG['listen']} "
          f"(mail={'dryrun' if CFG['dryrun'] else CFG['smtp_host']})", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
