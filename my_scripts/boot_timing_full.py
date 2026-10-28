#!/usr/bin/env python3
# 完整启动日志耗时分析脚本
# 用法：klogg 中选中完整启动 log -> 右键 -> Custom scripts -> boot_timing_full
# 输入：argv[1] 为选中文本所在临时文件路径
#
# 与 boot_timing_stats 的区别（面向包含 SDK 噪声的完整 log）：
#   1. 阶段树：marker 阶段按时间包含关系嵌套展示，输出自身耗时（扣除子阶段）
#   2. 静默空隙：相邻带时间戳日志行的最大间隔，定位"无日志但在干活"的隐藏耗时
#   3. 里程碑时间线：marker 一次性事件 + 预定义关键日志模式
import re
import sys
from datetime import datetime

MARKER = "CAMERA_PIPELINE_BOOT_TIMELINE_TRACE_MARKER"
TS_RE = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
KIND_RE = re.compile(r"\b(begin|end)\b")
# 提取日志正文：跳过 [时间戳] [级别] [文件:行 :函数] 三个括号段
MSG_RE = re.compile(r"^\[[^\]]*\]\s*\[[^\]]*\]\s*\[[^\]]*\]\s*(.*)$")

# 未打 marker 但值得关注的关键事件（正则 -> 友好名称）
MILESTONE_PATTERNS = [
    (r"Server: client connected", "IPC client 连接（UI 进程上线）"),
    (r"Load api bin Success", "IQ bin 加载成功"),
    (r"af parameter ready", "AF 标定参数就绪"),
    (r"calibrateParams completed", "ISP 标定参数完成"),
    (r"initISP\([^)]*\) Start", "SDK-ISP initISP 开始"),
    (r"initISP\([^)]*\) End", "SDK-ISP initISP 结束"),
    (r"IspTuneService.*init start", "IspTuneService 初始化开始"),
    (r"IspTuneService.*init end", "IspTuneService 初始化结束"),
    (r"initializing algorithmn components", "算法组件初始化开始"),
    (r"first framebuffer display", "首帧 framebuffer 显示"),
]


def parse_ts(text):
    return datetime.strptime(text, "%Y-%m-%d %H:%M:%S.%f")


def ms(offset):
    return offset.total_seconds() * 1000.0


def fmt_ts(ts):
    return ts.strftime("%H:%M:%S.%f")[:-3]


def short_msg(line):
    m = MSG_RE.match(line)
    msg = m.group(1) if m else line
    msg = msg.strip()
    return msg[:70] + ("..." if len(msg) > 70 else "")


def main():
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip("\n") for line in f]

    # 所有带时间戳的行：(时间, 原始行, 是否 marker)
    timestamped = []
    for line in lines:
        ts_match = TS_RE.match(line)
        if ts_match:
            timestamped.append((parse_ts(ts_match.group(1)), line, MARKER in line))

    if not timestamped:
        print("选中文本中没有找到带时间戳的日志行")
        return

    t0 = timestamped[0][0]
    total = ms(timestamped[-1][0] - t0)

    # ---------- marker 配对 ----------
    pending = {}     # label -> begin 时间
    stages = []      # [label, begin, end, extra]
    marker_milestones = []  # (时间, desc)
    for ts, line, _ in timestamped:
        if MARKER not in line:
            continue
        desc = line.split(MARKER, 1)[1].strip()
        kind_match = KIND_RE.search(desc)
        if kind_match is None:
            marker_milestones.append((ts, desc))
            continue
        label = desc[: kind_match.start()].strip()
        extra = desc[kind_match.end():].strip()
        if kind_match.group(1) == "begin":
            if label in pending:
                stages.append([label, pending.pop(label), None, ""])
            pending[label] = ts
        else:
            if label in pending:
                stages.append([label, pending.pop(label), ts, extra])
            else:
                marker_milestones.append((ts, desc))

    # ---------- 静默空隙 ----------
    gaps = []
    for i in range(1, len(timestamped)):
        dt = ms(timestamped[i][0] - timestamped[i - 1][0])
        if dt > 0:
            gaps.append((dt, timestamped[i - 1], timestamped[i]))

    # ---------- 模式里程碑 ----------
    pattern_milestones = []
    for ts, line, is_marker in timestamped:
        if is_marker:
            continue  # marker 行已由上面处理
        for pattern, name in MILESTONE_PATTERNS:
            if re.search(pattern, line):
                pattern_milestones.append((ts, name))
                break

    # ---------- 输出 ----------
    print("===== 完整启动日志耗时分析 =====")
    print("选中范围: {} -> {}".format(fmt_ts(t0), fmt_ts(timestamped[-1][0])))
    print("总耗时: {:.1f} ms   日志行数: {}   带时间戳行数: {}".format(
        total, len(lines), len(timestamped)))

    # 阶段树：按包含关系构建
    measured = [s for s in stages if s[2] is not None]
    measured.sort(key=lambda s: (s[1], -(s[2] - s[1]).total_seconds()))
    children = {i: [] for i in range(len(measured))}
    parent = [None] * len(measured)
    for i, s in enumerate(measured):
        for j in range(i):
            p = measured[j]
            # 父节点区间必须包含子节点且耗时严格更大，
            # 避免毫秒精度下 0ms 阶段互相误嵌套
            if p[1] <= s[1] and s[2] <= p[2] and (p[2] - p[1]) > (s[2] - s[1]):
                # 取最内层的父节点（区间最小的包含者）
                if parent[i] is None or (s[2] - s[1]) < (measured[parent[i]][2] - measured[parent[i]][1]):
                    parent[i] = j
        if parent[i] is not None:
            children[parent[i]].append(i)

    def stage_dur(i):
        return ms(measured[i][2] - measured[i][1])

    def print_tree(i, depth):
        label, begin, end, extra = measured[i]
        dur = stage_dur(i)
        child_sum = sum(stage_dur(c) for c in children[i])
        self_time = dur - child_sum
        pct = (dur / total * 100.0) if total > 0 else 0.0
        indent = "   " + "  |" * (depth - 1) + "  +-" if depth > 0 else "   "
        extra_str = "  ({})".format(extra) if extra else ""
        print("{}{}: {:.1f} -> {:.1f} ms  耗时 {:.1f} ms ({:.1f}%)  自身 {:.1f} ms{}".format(
            indent, label, ms(begin - t0), ms(end - t0), dur, pct, self_time, extra_str))
        for c in children[i]:
            print_tree(c, depth + 1)

    if measured:
        print()
        print("---- 阶段树（嵌套 = 包含关系，自身耗时已扣除子阶段）----")
        for i in range(len(measured)):
            if parent[i] is None:
                print_tree(i, 0)

    # 最耗时 Top3（按总耗时）
    if measured:
        print()
        print("---- 最耗时阶段 Top3 ----")
        for i in sorted(range(len(measured)), key=lambda x: -stage_dur(x))[:3]:
            print("{:>8.1f} ms   {}".format(stage_dur(i), measured[i][0]))

    # 静默空隙
    if gaps:
        print()
        print("---- 静默空隙 Top8（相邻日志行最大间隔，定位无日志的隐藏耗时）----")
        for dt, prev, cur in sorted(gaps, key=lambda g: -g[0])[:8]:
            print("{:>8.1f} ms   {} -> {}".format(dt, fmt_ts(prev[0]), fmt_ts(cur[0])))
            print("          前: {}".format(short_msg(prev[1])))
            print("          后: {}".format(short_msg(cur[1])))

    # 里程碑时间线
    milestones = marker_milestones + pattern_milestones
    if milestones:
        print()
        print("---- 里程碑时间线 ----")
        for ts, desc in sorted(milestones, key=lambda m: m[0]):
            print("{:>9.1f} ms   {}".format(ms(ts - t0), desc))

    # 未配对警告
    if pending:
        print()
        print("---- 警告：以下阶段只有 begin 没有 end ----")
        for label, begin in pending.items():
            print("{:>9.1f} ms   {}".format(ms(begin - t0), label))
    missing_end = [s for s in stages if s[2] is None]
    if missing_end:
        print("---- 警告：以下阶段被同名 begin 顶替关闭（疑似重复开机或漏打 end）----")
        for label, begin, _, _ in missing_end:
            print("{:>9.1f} ms   {}".format(ms(begin - t0), label))


if __name__ == "__main__":
    main()
