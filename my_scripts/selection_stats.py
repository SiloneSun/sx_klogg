#!/usr/bin/env python3
# 示例脚本：统计选中文本的基本信息
# 用法：klogg 右键 -> Custom scripts -> selection_stats
# 输入：argv[1] 为选中文本所在临时文件的路径
import sys
from collections import Counter


def main():
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip("\n") for line in f]

    total_chars = sum(len(line) for line in lines)
    level_counter = Counter()
    for line in lines:
        upper = line.upper()
        for level in ("ERROR", "WARN", "INFO", "DEBUG"):
            if level in upper:
                level_counter[level] += 1
                break

    print("===== 选中文本统计 =====")
    print(f"行数      : {len(lines)}")
    print(f"总字符数  : {total_chars}")
    if lines:
        print(f"首行      : {lines[0][:80]}")
        print(f"末行      : {lines[-1][:80]}")
    if level_counter:
        print("----- 日志级别 -----")
        for level in ("ERROR", "WARN", "INFO", "DEBUG"):
            if level_counter[level]:
                print(f"{level:<6}: {level_counter[level]}")


if __name__ == "__main__":
    main()
