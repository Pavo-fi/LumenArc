#pragma once

#include <QString>

// 署名（姓名+单位）——独立单一真源（账号系统 v1.3 拍板：署名作为单独值记录）。
// 接入点：案件录入（调查员/单位）→ 报告「分析人」落款、点位图「制图」栏；
// 后续新接入点一律从 SignatureStore 取，不再各自读账号信息。
// 与账号档案的关系：登录成功/资料更新时，仅当署名为空才用账号姓名/单位初始化
// （用户自设的署名不被覆盖）；署名可在「账号管理」里本地修改（免验证码）。
class SignatureStore {
public:
    static QString name();
    static QString org();
    static void save(const QString& name, const QString& org);
    static bool empty();
    // 署名是空值时用给定姓名/单位初始化；已有值则不动。返回是否发生了初始化。
    static bool initIfEmpty(const QString& name, const QString& org);
};
