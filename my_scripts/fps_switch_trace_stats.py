#!/usr/bin/env python3
# FPS 切换耗时统计脚本：优先分析 onSetMainFPS -> first framebuffer display 的完整事务。
# 用法：Klogg 中选中包含一次或多次 FPS 切换的日志 -> Custom scripts -> fps_switch_trace_stats
# 输入：argv[1] 为选中文本所在临时文件路径。

import re
import sys
from datetime import datetime


MARKER = "[fps_switch_trace]"
TS_RE = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
FPS_REQUEST_RE = re.compile(
    r"(?:onSetMainFPS.*\[Main\]\s+fps\s*=\s*(\d+)|"
    r"\[Main\]\s+fps\s*=\s*(\d+).*onSetMainFPS)"
)
ENTER_RE = re.compile(r"change_data_link enter root=(\S+)")
LEAVE_RE = re.compile(r"change_data_link leave root=(\S+)")
STAGE_RE = re.compile(
    r"change_data_link (stop|save connections|disconnect|callback|reconnect|start) "
    r"(begin|end) root=(\S+)"
)
API_RE = re.compile(
    r"(MI_SYS_UnBindChnPort|MI_SYS_BindChnPort2|MI_SNR_SetFps) (begin|end)(.*)"
)
ELEMENT_START_RE = re.compile(r"element start (begin|end) name=(\S+)(?:\s+ret=(\d+))?")
NODE_STEP_RE = re.compile(
    r"(vif enable output port|isp start_loadbin|scl MI_SCL_StartChannel) "
    r"(begin|end) name=(\S+)(.*)"
)
ELEMENT_DISCONNECT_RE = re.compile(r"element disconnect (begin|end) name=(\S+)")
PORT_DISCONNECT_RE = re.compile(
    r"port disconnect (complete_disconnect|disconnect_up|after_disconnect) "
    r"(begin|end) (.*)"
)
FIRST_FRAME = "first framebuffer display"
RELEASE_DONE = "[vrec_release][video] done"
INVALIDATE_DONE = "invalidated video branch;"
PREPARE_VIDEO_ENTER = "[vrec_prepare][video] enter"
STAGE_ORDER = ("stop", "save connections", "disconnect", "callback", "reconnect", "start")


def parse_ts(text):
    return datetime.strptime(text, "%Y-%m-%d %H:%M:%S.%f")


def duration_ms(begin, end):
    return (end - begin).total_seconds() * 1000.0


def format_ts(ts):
    return ts.strftime("%H:%M:%S.%f")[:-3]


def parse_timestamped(lines):
    events = []
    for index, line in enumerate(lines):
        match = TS_RE.search(line)
        if match:
            events.append((parse_ts(match.group(1)), index, line))
    return sorted(events, key=lambda event: (event[0], event[1]))


def api_key(name, details):
    src_dst = re.search(r"src=(\S+)\s+dst=(\S+)", details)
    if src_dst:
        return name, "{} -> {}".format(src_dst.group(1), src_dst.group(2))
    sensor = re.search(r"sensor=(\d+)", details)
    if sensor:
        return name, "sensor={}".format(sensor.group(1))
    return name, details.strip() or "unknown"


def parse_switches(timestamped):
    active = []
    switches = []
    for ts, _, line in timestamped:
        if MARKER not in line:
            continue
        message = line.split(MARKER, 1)[1].strip()
        enter = ENTER_RE.search(message)
        if enter:
            switch = {"root": enter.group(1), "begin": ts, "end": None, "events": [(ts, message)]}
            switches.append(switch)
            active.append(switch)
            continue

        for switch in active:
            switch["events"].append((ts, message))

        leave = LEAVE_RE.search(message)
        if leave:
            for index in range(len(active) - 1, -1, -1):
                if active[index]["root"] == leave.group(1):
                    active[index]["end"] = ts
                    active.pop(index)
                    break
    return switches


def parse_transactions(timestamped):
    transactions = []
    for ts, _, line in timestamped:
        match = FPS_REQUEST_RE.search(line)
        if not match:
            continue
        fps = next(value for value in match.groups() if value is not None)
        transactions.append({"begin": ts, "target_fps": int(fps), "end": None})

    for index, transaction in enumerate(transactions):
        next_begin = transactions[index + 1]["begin"] if index + 1 < len(transactions) else None
        for ts, _, line in timestamped:
            if ts < transaction["begin"]:
                continue
            if next_begin is not None and ts >= next_begin:
                break
            if FIRST_FRAME in line:
                transaction["end"] = ts
                break
    return transactions


def find_event(timestamped, begin, end, needle):
    for ts, _, line in timestamped:
        if ts < begin:
            continue
        if end is not None and ts > end:
            break
        if needle in line:
            return ts
    return None


def switch_for_transaction(switches, transaction, next_transaction):
    boundary = next_transaction["begin"] if next_transaction is not None else None
    for switch in switches:
        if switch["begin"] < transaction["begin"]:
            continue
        if boundary is not None and switch["begin"] >= boundary:
            break
        return switch
    return None


def pair_stages(switch):
    pending = {}
    stages = {}
    warnings = []
    for ts, message in switch["events"]:
        match = STAGE_RE.search(message)
        if not match or match.group(3) != switch["root"]:
            continue
        name, kind = match.group(1), match.group(2)
        if kind == "begin":
            pending[name] = ts
        elif name in pending:
            stages[name] = duration_ms(pending.pop(name), ts)
        else:
            warnings.append("阶段 {} 只有 end".format(name))
    for name in pending:
        warnings.append("阶段 {} 只有 begin".format(name))
    return stages, warnings


def pair_apis(switch):
    pending = {}
    calls = []
    warnings = []
    for ts, message in switch["events"]:
        match = API_RE.search(message)
        if not match:
            continue
        name, kind, details = match.groups()
        key = api_key(name, details)
        if kind == "begin":
            pending[key] = ts
        elif key in pending:
            calls.append((key[0], key[1], duration_ms(pending.pop(key), ts)))
        else:
            warnings.append("{} {} 只有 end".format(key[0], key[1]))
    for name, detail in pending:
        warnings.append("{} {} 只有 begin".format(name, detail))
    return calls, warnings


def pair_node_starts(switch):
    pending = {}
    starts = []
    warnings = []
    for ts, message in switch["events"]:
        match = ELEMENT_START_RE.search(message)
        if not match:
            continue
        kind, name, _ = match.groups()
        if kind == "begin":
            pending[name] = ts
        elif name in pending:
            starts.append((name, duration_ms(pending.pop(name), ts)))
        else:
            warnings.append("节点 {} start 只有 end".format(name))
    for name in pending:
        warnings.append("节点 {} start 只有 begin".format(name))
    return starts, warnings


def pair_node_steps(switch):
    pending = {}
    steps = []
    warnings = []
    for ts, message in switch["events"]:
        match = NODE_STEP_RE.search(message)
        if not match:
            continue
        operation, kind, name, details = match.groups()
        port_match = re.search(r"port=(\d+)", details)
        key = operation, name, port_match.group(1) if port_match else ""
        if kind == "begin":
            pending[key] = ts
        elif key in pending:
            steps.append((operation, name, key[2], duration_ms(pending.pop(key), ts)))
        else:
            warnings.append("{} {} 只有 end".format(operation, name))
    for operation, name, _ in pending:
        warnings.append("{} {} 只有 begin".format(operation, name))
    return steps, warnings


def disconnect_key(operation, details):
    up_down = re.search(r"up=(\S+)\s+down=(\S+)", details)
    if up_down:
        return operation, "{} -> {}".format(up_down.group(1), up_down.group(2))
    down_up = re.search(r"down=(\S+)\s+up=(\S+)", details)
    if down_up:
        return operation, "{} <- {}".format(down_up.group(1), down_up.group(2))
    return operation, details.strip() or "unknown"


def pair_node_disconnects(switch):
    pending_nodes = {}
    pending_steps = {}
    nodes = []
    steps = []
    warnings = []
    for ts, message in switch["events"]:
        node_match = ELEMENT_DISCONNECT_RE.search(message)
        if node_match:
            kind, name = node_match.groups()
            if kind == "begin":
                pending_nodes[name] = ts
            elif name in pending_nodes:
                nodes.append((name, duration_ms(pending_nodes.pop(name), ts)))
            else:
                warnings.append("节点 {} disconnect 只有 end".format(name))
            continue

        step_match = PORT_DISCONNECT_RE.search(message)
        if not step_match:
            continue
        operation, kind, details = step_match.groups()
        key = disconnect_key(operation, details)
        if kind == "begin":
            pending_steps[key] = ts
        elif key in pending_steps:
            steps.append((key[0], key[1], duration_ms(pending_steps.pop(key), ts)))
        else:
            warnings.append("{} {} 只有 end".format(key[0], key[1]))

    for name in pending_nodes:
        warnings.append("节点 {} disconnect 只有 begin".format(name))
    for operation, link in pending_steps:
        warnings.append("{} {} 只有 begin".format(operation, link))
    return nodes, steps, warnings


def print_switch_details(switch):
    stages, stage_warnings = pair_stages(switch)
    if stages:
        print()
        print("---- change_data_link 内部 ----")
        for name in STAGE_ORDER:
            if name in stages:
                print("{:<29}{:.1f} ms".format(name, stages[name]))

    calls, api_warnings = pair_apis(switch)
    if calls:
        print()
        print("---- 底层 API 耗时 ----")
        for name, detail, elapsed in calls:
            print("{} {:<18} {:.1f} ms".format(name, detail, elapsed))

    starts, start_warnings = pair_node_starts(switch)
    if starts:
        print()
        print("---- 节点 start 耗时 ----")
        for name, elapsed in starts:
            print("{:<29}{:.1f} ms".format(name, elapsed))

    steps, step_warnings = pair_node_steps(switch)
    if steps:
        print()
        print("---- 节点内部 API 耗时 ----")
        for operation, name, port, elapsed in steps:
            suffix = " port={}".format(port) if port else ""
            print("{} {}{} {:.1f} ms".format(operation, name, suffix, elapsed))

    disconnect_nodes, disconnect_steps, disconnect_warnings = pair_node_disconnects(switch)
    if disconnect_nodes:
        print()
        print("---- 节点 disconnect 耗时 ----")
        for name, elapsed in disconnect_nodes:
            print("{:<29}{:.1f} ms".format(name, elapsed))

    if disconnect_steps:
        print()
        print("---- 端口 disconnect 分段耗时 ----")
        for operation, link, elapsed in disconnect_steps:
            print("{} {} {:.1f} ms".format(operation, link, elapsed))

    warnings = stage_warnings + api_warnings + start_warnings + step_warnings + disconnect_warnings
    if warnings:
        print()
        print("---- 警告 ----")
        for warning in warnings:
            print(warning)
    return stages


def print_transaction(index, transaction, switch, timestamped):
    print("===== FPS 切换事务 #{}：目标 {} fps =====".format(index, transaction["target_fps"]))
    if transaction["end"] is None:
        print("范围: {} -> --  [缺失 first framebuffer display]".format(format_ts(transaction["begin"])))
        print()
        return None

    begin = transaction["begin"]
    end = transaction["end"]
    release_done = find_event(timestamped, begin, end, RELEASE_DONE)
    invalidate_done = find_event(timestamped, begin, end, INVALIDATE_DONE)
    prepare_video = find_event(timestamped, begin, end, PREPARE_VIDEO_ENTER)

    print("范围: {} -> {}".format(format_ts(begin), format_ts(end)))
    print("端到端耗时: {:.1f} ms".format(duration_ms(begin, end)))
    print()
    print("---- 关键里程碑 ----")
    if release_done is not None:
        print("FPS 请求 -> 旧 VENC 视频支路释放完成    {:.1f} ms".format(duration_ms(begin, release_done)))
    if invalidate_done is not None:
        print("FPS 请求 -> 旧预热资源失效完成          {:.1f} ms".format(duration_ms(begin, invalidate_done)))
    if prepare_video is not None and switch is not None:
        print("预热 video 分支进入 -> change_data_link 开始    {:.1f} ms".format(
            duration_ms(prepare_video, switch["begin"])))
    if switch is not None and switch["end"] is not None:
        print("change_data_link 总耗时    {:.1f} ms".format(duration_ms(switch["begin"], switch["end"])))
        print("图链重建完成 -> first framebuffer display    {:.1f} ms".format(
            duration_ms(switch["end"], end)))
    elif switch is None:
        print("[警告] 本事务中未找到 change_data_link enter")
    else:
        print("[警告] change_data_link 缺失 leave")

    if switch is not None:
        print_switch_details(switch)
    print()
    return duration_ms(begin, end)


def print_legacy_switch(index, switch, timestamped, next_switch):
    print("===== FPS 切换 #{}：{} =====".format(index, switch["root"]))
    if switch["end"] is None:
        print("范围: {} -> --  [缺失 leave]".format(format_ts(switch["begin"])))
    else:
        print("范围: {} -> {}".format(format_ts(switch["begin"]), format_ts(switch["end"])))
        print("change_data_link 总耗时: {:.1f} ms".format(duration_ms(switch["begin"], switch["end"])))
    print_switch_details(switch)

    if switch["end"] is not None:
        boundary = next_switch["begin"] if next_switch is not None else None
        first_frame = find_event(timestamped, switch["end"], boundary, FIRST_FRAME)
        if first_frame is not None:
            print()
            print("---- 预览恢复 ----")
            graph_start_end = next(
                (ts for ts, message in switch["events"] if "change_data_link start end" in message),
                switch["end"],
            )
            print("graph start end -> first framebuffer display: {:.1f} ms".format(
                duration_ms(graph_start_end, first_frame)))
            print("change_data_link enter -> first framebuffer display: {:.1f} ms".format(
                duration_ms(switch["begin"], first_frame)))
    print()


def main():
    if len(sys.argv) != 2:
        print("用法: {} <Klogg 选中文本临时文件>".format(sys.argv[0]))
        return 2

    with open(sys.argv[1], encoding="utf-8", errors="replace") as selected_log:
        timestamped = parse_timestamped([line.rstrip("\n") for line in selected_log])
    if not timestamped:
        print("选中文本中没有找到带时间戳的日志行")
        return 1

    switches = parse_switches(timestamped)
    transactions = parse_transactions(timestamped)
    if transactions:
        print("===== FPS 切换端到端耗时统计 =====")
        print("识别到 {} 次 onSetMainFPS 请求".format(len(transactions)))
        print()
        totals = []
        for index, transaction in enumerate(transactions, start=1):
            next_transaction = transactions[index] if index < len(transactions) else None
            switch = switch_for_transaction(switches, transaction, next_transaction)
            total = print_transaction(index, transaction, switch, timestamped)
            if total is not None:
                totals.append(total)
        if len(totals) > 1:
            print("===== 端到端汇总 =====")
            print("平均 {:.1f} ms   最大 {:.1f} ms   样本 {}".format(
                sum(totals) / len(totals), max(totals), len(totals)))
        return 0

    if not switches:
        print("选中文本中没有找到 onSetMainFPS 或 [fps_switch_trace] change_data_link enter 日志")
        return 1

    print("===== FPS 切换耗时统计（未找到 onSetMainFPS，使用图链回退模式） =====")
    print("识别到 {} 轮 change_data_link".format(len(switches)))
    print()
    for index, switch in enumerate(switches, start=1):
        next_switch = switches[index] if index < len(switches) else None
        print_legacy_switch(index, switch, timestamped, next_switch)
    return 0


if __name__ == "__main__":
    sys.exit(main())
