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
| authRegister | 注册/登录合一 | 校验短信验证码（CloudBase Auth HTTP 通道）→ upsert users → 发 token（30 天） |
| authHeartbeat | 心跳/续期 | 验 token → 更新 lastSeenAt/版本 → 返回服务端时间戳 |
| inviteActivate | 邀请码激活 | 验 code → 登记 users(source=invite) → 发长期 token |
| feedback | 意见反馈 | 验 token → 写 feedback 表 |

## token 设计

云函数签发：base64(json{phone, exp, nonce}) + "." + HMAC-SHA256(secret)
- secret 放云函数环境变量 `AUTH_SECRET`（控制台配置，勿入库）
- 短信用户 token 30 天；邀请码用户 180 天（离线友好）
- 心跳验签+查 status；过期/停用 → 401，客户端强制重新登录

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
