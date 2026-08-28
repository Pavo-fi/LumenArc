#pragma once

#include <QString>

// 登录凭证本地存储 + 30 天策略（账号系统 v1）。
// 策略（拍板）：token 自身 30 天有效；距上次成功在线验证 ≥30 天也必须重新短信登录；
// 断网且在宽限期内放行，断网且超期锁定（须联网重新登录）。
struct Credential {
    QString token;
    qint64 expiresAtMs = 0;   // 服务端签发的过期时刻
    qint64 lastOkAtMs = 0;    // 上次在线验证成功时刻（心跳或登录）
    QString uid;              // 手机号或 invite:<code>
    QString name;
    QString org;
    bool valid() const { return !token.isEmpty(); }
};

class CredentialStore {
public:
    static constexpr qint64 kReverifyWindowMs = 30LL * 24 * 3600 * 1000;  // 30 天

    static Credential load();
    static void save(const Credential& c);
    static void clear();
    static void touchOk(qint64 nowMs);  // 在线验证成功，刷新 lastOkAt（及可能的续签 token）

    // 启动判决：
    //   Pass        —— 直接放行（心跳异步做）
    //   NeedLogin   —— 本地可判必须重新登录（无 token / token 过期 / 超 30 天未验证）
    enum class Verdict { Pass, NeedLogin };
    static Verdict startupVerdict(const Credential& c, qint64 nowMs);
};
