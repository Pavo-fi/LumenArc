# LumenArc 账号与反馈系统 — 服务端（腾讯云开发 CloudBase）

## 架构

C++ 客户端 ──HTTPS POST──> HTTP 触发器云函数 ──> 云数据库（users / invites / feedback）
管理端：CloudBase 控制台直接查表/导出（无自建后台）。

## 数据表

### users（用户台账）
| 字段 | 类型 | 说明 |
|------|------|------|
| phone | string | 手机号（唯一键；建议控制台加唯一索引） |
| name | string | 姓名 |
| org | string | 单位 |
| source | string | `sms`=注册 / `invite`=邀请码 |
| createdAt | number | 注册时间（epoch ms） |
| lastSeenAt | number | 最近活跃（心跳写入） |
| lastVersion | string | 在用版本 |
| lastPlatform | string | 系统信息（如 "Windows 11 23H2 x64"） |
| status | string | `active` / `disabled`（停用即拒绝登录） |

### invites（邀请码）
| 字段 | 类型 | 说明 |
|------|------|------|
| code | string | 邀请码（唯一索引），格式建议 `LA-XXXX-XXXX` |
| forName / forOrg | string | 预分配给谁的备注 |
| usedBy | string | 已使用手机号/标识（空=未用） |
| createdAt / usedAt | number | |

### feedback（意见反馈）
| 字段 | 类型 | 说明 |
|------|------|------|
| phone / name | string | 提交人 |
| version / platform | string | 环境 |
| text | string | 正文 |
| diag | object | 可选诊断信息（版本/日志摘要/崩溃列表，**绝不含案件数据**） |
| status | string | `new` / `read` / `done` |
| createdAt | number | |

## 云函数（本目录 functions/ 下，逐个建 HTTP 触发器函数粘贴部署）

| 函数 | 端点用途 | 说明 |
|------|---------|------|
| authRegister | 注册/登录合一 | 验 CloudBase access_token（调 user/me）→ upsert users → 发 token（30 天） |
| authHeartbeat | 心跳/续期 | 验 token → 更新 lastSeenAt/版本 → 距到期<7天自动续签 |
| inviteActivate | 邀请码激活 | 验 code → 登记 users(source=invite) → 发长期 token |
| feedback | 意见反馈 | 验 token → 写 feedback 表 |

## token 设计

云函数签发：`base64url(JSON{phone,kind,exp,nonce}) + "." + HMAC-SHA256-hex`
- secret 放云函数环境变量 `AUTH_SECRET`（勿入库）
- 短信用户 token 30 天；邀请码用户 180 天（离线友好）
- 心跳验签+查 status；过期/停用 → 401，客户端强制重新登录

## 短信链路（客户端直连 CloudBase 身份验证网关，联调实锤 2026-08-28）

网关 base：`https://<env>.api.tcloudbasegateway.com/auth/v1`，统一 query `?client_id=<env>`，
头部 `x-device-id`（稳定设备指纹）。源自 @cloudbase/js-sdk 3.9.0 源码拦截实测。
1. `POST /verification` `{phone_number:"+86 1XX"}` → 发短信，得 `verification_id`
2. `POST /verification/verify` `{phone_number, verification_code}` → `verification_token, is_user`
3. 老用户 `POST /signin` `{username:"+86 1XX", verification_token}` → `access_token`；
   新用户先 `POST /signup` `{phone_number, verification_token, verification_code}` 再 signin
4. 客户端把 `access_token` 交 authRegister：服务端 `GET /user/me`（Bearer）验真伪取手机号

## 部署实况（2026-08-28，CLI 自动部署完成）

- 环境 ID：`lumenarc-prod-d6gcdfb6a8873d906`（上海，体验版）
- 四个函数已部署 + HTTP 触发器已通（负向测试全过）：
  - `https://lumenarc-prod-d6gcdfb6a8873d906.service.tcloudbase.com/authRegister`
  - `.../authHeartbeat`  `.../inviteActivate`  `.../feedback`
- 集合 users/invites/feedback 已由探针函数创建（已验证文档型云数据库可用）
- AUTH_SECRET 已配（存于 build_tmp/tcb_deploy/.auth_secret，不入库）
- 改动后重新部署：`cd build_tmp/tcb_deploy && tcb fn deploy <name> --path /<name> --force --yes`
  （cygwin 下需 `MSYS_NO_PATHCONV=1`，否则 /path 被转成 Windows 路径）
- 唯一剩下的控制台手点：**身份验证 → 登录方式 → 手机短信验证码 → 开启**
  （tcb API 无此开关，DescribeAuthProviders 等均 InvalidAction）

## 成本估算

- 短信：约 0.045 元/条 ×（注册 1 + 每月 1）≈ 每用户每年 0.6 元
- 云函数/数据库：免费额度内（用户量 <1000 无压力）

## 联调实锤坑位（2026-08-28 客户端联调）

1. **URL 拼接顺序**：`?client_id=` 必须最后加（先加再拼路径 → 路径掉进 query，
   网关报 `invalid client id`，客户端误显示"验证码错误"）
2. **异步回调禁止引用捕获栈变量**（`[&gb]` 悬空 → Qt6Core 0xC0000005，崩在固定地址）；
   排查法：崩溃黑匣子 stage 面包屑 + 同款代码本地测试台（build_tmp/cloudtest）+
   本地回显服务器抓包 + curl 同参二分
3. verify 必须回传 `verification_id`；verify 响应**不带** `is_user`；
   signin 对新用户报 NOT_FOUND → 转 signup（signup 直接返回 access_token）
4. 同手机号反复发码会互相作废旧码；发码有限流（RESOURCE_EXHAUSTED）
5. 客户端 env 覆盖调试钩子：`LUMENARC_CLOUD_GATEWAY` / `LUMENARC_CLOUD_SERVICE`；
   编译加 `LUMENARC_CLOUD_DEBUG` 宏可 dump 请求/响应原文
