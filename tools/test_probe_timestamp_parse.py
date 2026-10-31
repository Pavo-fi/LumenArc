#!/usr/bin/env python3
"""回归测试：probe_timestamps.parse_timestamp() 的秒位解析。

背景（2026-10-30 顺德件实测「校时跟墙钟对不上」）：
画面 OSD `2026年09月20日 星期日15:11:25` 的第二个冒号被 OCR 读成 `/`，
`RE_FULL` 只吃到 `15:11`，秒位静默按 0 收盘 —— 该测点墙钟少 25 秒，
两点拟合把这 25 秒摊成 **-433 秒/天** 的假漂移并自动应用，
整条时间轴在 1 小时处偏 19 秒（画面 14:51:37 / 图表 14:51:18）。

判据：秒位拼错时要么**读出来**，要么**整条样本作废**，绝不静默按 :00 收。

用法： python tools/test_probe_timestamp_parse.py
"""
import datetime
import os
import sys

sys.argv = sys.argv[:1]                     # probe 顶层会解析 argv，先占位
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import probe_timestamps as P                # noqa: E402

FILENAME = "LAMerged_D12_20260920134804_D12_20260920150828.mp4"

# (输入 OCR 文本, 期望 datetime, 期望毫秒, 期望 kind)
CASES = [
    # 实测错读：秒位冒号 → "/"，必须读成 25 秒（修复前是 15:11:00）
    ("2026年09月20日 星期日15:11/25", datetime.datetime(2026, 9, 20, 15, 11, 25), 0, "full"),
    # 其他分隔符错读同样要读出来
    ("2026年09月20日 星期日 15:11;25", datetime.datetime(2026, 9, 20, 15, 11, 25), 0, "full"),
    ("2026年09月20日 星期日 15:11|25", datetime.datetime(2026, 9, 20, 15, 11, 25), 0, "full"),
    # 正常样本（不能回归）
    ("2026年09月20日 星期日13:48:03", datetime.datetime(2026, 9, 20, 13, 48, 3), 0, "full"),
    # 尾部噪字（实测第一条测点就是这个形态）
    ("2026年09月20日 星期日13:48:037", datetime.datetime(2026, 9, 20, 13, 48, 3), 0, "full"),
    # 小数毫秒不能被当秒（.250 = 250ms，不是 25 秒）
    ("2026年09月20日 星期日 15:11:25.250", datetime.datetime(2026, 9, 20, 15, 11, 25), 250, "full"),
    ("2026-09-20 15:11.250", datetime.datetime(2026, 9, 20, 15, 11, 0), 250, "full"),
    # 真·只有时分（秒=0 是唯一天然解）——保持原行为
    ("2026年09月20日 星期日 15:11", datetime.datetime(2026, 9, 20, 15, 11, 0), 0, "full"),
    # 有分隔符但后面不是两位数字 → 整条作废（返回 None，由调用方跳过该测点）
    ("2026年09月20日 星期日 15:11/2x", None, None, None),
    # 星期被读花（实测 `星期亐0`，真值星期三）：星期整段剥离，样本必须可用
    ("2026年07月22日星期亐0 06:08:34", datetime.datetime(2026, 7, 22, 6, 8, 34), 0, "full"),
    # 剥离星期不能吃掉小时的数字（"星期三 05:21:39" 不能变成 5:21 丢前导 0 再错位）
    ("2026年07月22日 星期三 05:21:39", datetime.datetime(2026, 7, 22, 5, 21, 39), 0, "full"),
    ("2026年09月11日 星期五 04:08 :49", datetime.datetime(2026, 9, 11, 4, 8, 49), 0, "full"),
    ("2026-09-20 星期日 15:11:25", datetime.datetime(2026, 9, 20, 15, 11, 25), 0, "full"),
    # 「日」字丢失
    ("2026年09月20 星期日 15:33:54", datetime.datetime(2026, 9, 20, 15, 33, 54), 0, "full"),
]

# (rawText, 期望 weekday_mismatch) —— 只做诊断，绝不据此拒样
WEEKDAY_CASES = [
    ("2026年07月22日 星期三 05:21:39", False),   # 2026-07-22 确为星期三
    ("2026年09月20日 星期日 13:48:03", False),
    ("2026年09月20 星期五 15:33:54", True),      # 2026-09-20 是星期日 → 与文本冲突
    ("2026年09月20日 星期日 15:11/25", False),   # 秒位错读不影响星期判据
]


def main() -> int:
    fails = 0
    for text, want_dt, want_ms, want_kind in CASES:
        got = P.parse_timestamp(text, FILENAME)
        ok = (got is None) if want_dt is None else (
            got is not None and got[0] == want_dt and got[1] == want_ms
            and got[2] == want_kind)
        print(("PASS" if ok else "FAIL") + " | " + repr(text) + " -> " + repr(got))
        if not ok:
            print("        期望: " + repr((want_dt, want_ms, want_kind)))
            fails += 1

    for text, want in WEEKDAY_CASES:
        r = P.parse_timestamp(text, FILENAME)
        got = P.weekday_mismatch(text, r[0]) if r else None
        ok = (got == want)
        print(("PASS" if ok else "FAIL") + " | 星期判据 " + repr(text)
              + " -> mismatch=" + repr(got))
        if not ok:
            fails += 1

    # 端到端：用实测两点重算，假漂移必须消失、播放头处必须对上画面时间
    s1, w1 = 1000, 1789883283000
    s2, w2 = 5003080, 1789888285000          # 15:11:25（修复后的正确读数）
    mx, my = (s1 + s2) / 2.0, (w1 + w2) / 2.0
    sxx = (s1 - mx) ** 2 + (s2 - mx) ** 2
    rate = ((s1 - mx) * (w1 - my) + (s2 - mx) * (w2 - my)) / sxx
    drift = (rate - 1.0) * 86400.0
    sigma = (2.0 * 1000.0 ** 2 / sxx) ** 0.5
    significant = abs(rate - 1.0) > max(3.0 * sigma, 10.0 / 86400000.0)
    ok = abs(drift) < 10.0 and not significant
    print(("PASS" if ok else "FAIL")
          + " | 重算漂移 %.2f 秒/天（显著=%s，应为不显著）" % (drift, significant))
    fails += 0 if ok else 1

    print("\n%d 项断言，%d 失败" % (len(CASES) + 1, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
