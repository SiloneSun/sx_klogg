#!/usr/bin/env python3
# 启动耗时统计脚本：解析 CAMERA_PIPELINE_BOOT_TIMELINE_TRACE_MARKER 启动日志
# 用法：klogg 中选中启动 log 片段 -> 右键 -> Custom scripts -> boot_timing_stats
# 输入：argv[1] 为选中文本所在临时文件路径
#
# 解析规则：
#   1. 只处理含 CAMERA_PIPELINE_BOOT_TIMELINE_TRACE_MARKER 的行
#   2. "... begin [附加信息]" / "... end [附加信息]" 按阶段名配对，计算各阶段耗时
#   3. 无 begin/end 的 marker 行视为一次性里程碑（如首帧显示），输出相对时刻
#   4. 同名阶段重复出现（多次开机）时，先关闭上一次未配对的再重新打开
import re
import sys
from datetime import datetime

MARKER = "CAMERA_PIPELINE_BOOT_TIMELINE_TRACE_MARKER"
TS_RE = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
KIND_RE = re.compile(r"\b(begin|end)\b")


def parse_ts(text):
    return datetime.strptime(text, "%Y-%m-%d %H:%M:%S.%f")


def ms(offset):
    return offset.total_seconds() * 1000.0


def main():
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    events = []
    for line in lines:
        if MARKER not in line:
            continue
        ts_match = TS_RE.search(line)
        if not ts_match:
            continue
        desc = line.split(MARKER, 1)[1].strip()
        events.append((parse_ts(ts_match.group(1)), desc))

    if not events:
        print("选中文本中没有找到 boot marker 日志")
        return

    t0 = events[0][0]
    total = ms(events[-1][0] - t0)

    pending = {}     # label -> begin 时间
    stages = []      # (label, begin, end, extra)
    milestones = []  # (时间, desc)
    for ts, desc in events:
        kind_match = KIND_RE.search(desc)
        if kind_match is None:
            milestones.append((ts, desc))
            continue
        label = desc[: kind_match.start()].strip()
        extra = desc[kind_match.end():].strip()
        if kind_match.group(1) == "begin":
            if label in pending:
                # 同名阶段重复出现：关闭上一个（视为无 end），重新开始
                stages.append((label, pending.pop(label), None, ""))
            pending[label] = ts
        else:  # end
            if label in pending:
                stages.append((label, pending.pop(label), ts, extra))
            else:
                milestones.append((ts, desc))

    print("===== 启动耗时统计 =====")
    print("选中范围: {} -> {}".format(t0.strftime("%H:%M:%S.%f")[:-3],
                                       events[-1][0].strftime("%H:%M:%S.%f")[:-3]))
    print("marker 事件数: {}   总耗时: {:.1f} ms".format(len(events), total))

    # 阶段耗时表（按 begin 时刻排序）
    ordered = sorted(stages, key=lambda s: s[1])
    if ordered:
        print()
        print("---- 阶段耗时（按 begin 时刻排序）----")
        for label, begin, end, extra in ordered:
            b = ms(begin - t0)
            if end is None:
                print("{:>9.1f} ms         --      {}   [缺失 end!]".format(b, label))
                continue
            e = ms(end - t0)
            d = e - b
            pct = (d / total * 100.0) if total > 0 else 0.0
            extra_str = "   ({})".format(extra) if extra else ""
            print("{:>9.1f} -> {:>9.1f} ms   耗时 {:>8.1f} ms ({:5.1f}%)   {}{}".format(
                b, e, d, pct, label, extra_str))

    # 耗时 Top3
    measured = [(label, ms(end - begin)) for label, begin, end, _ in ordered if end]
    if measured:
        print()
        print("---- 最耗时 Top3 ----")
        for label, d in sorted(measured, key=lambda x: -x[1])[:3]:
            print("{:>8.1f} ms   {}".format(d, label))

    # 一次性里程碑
    if milestones:
        print()
        print("---- 里程碑事件 ----")
        for ts, desc in milestones:
            print("{:>9.1f} ms   {}".format(ms(ts - t0), desc))

    # 未配对的 begin
    if pending:
        print()
        print("---- 警告：以下阶段只有 begin 没有 end ----")
        for label, begin in pending.items():
            print("{:>9.1f} ms   {}".format(ms(begin - t0), label))


if __name__ == "__main__":
    main()
