#!/usr/bin/env python3
"""推送前泄漏扫描：查个人信息、作者机器路径、凭据。

用法:
    python3 docs/leak_scan.py            # 扫描仓库根
    python3 docs/leak_scan.py <目录>

退出码 0 = 干净可推，1 = 有待处理项。

模式分两类：
  PATTERNS            通用结构模式，不含任何具体姓名/机构，可安全入库。
  .leak-patterns.local  私有名单（真实姓名、机构、证件号），不入库。

本脚本**不豁免自己**——扫描器把敏感名单写进自身、再跳过自查，
会产出「一切正常」的假象。名单外置正是为了避免这一点。
"""
import os
import re
import sys

# 通用模式：不含任何具体姓名/机构，可安全入库
PATTERNS = [
    ('Windows 盘符路径', re.compile('[\'"]\\s*[A-Za-z]:[\\\\/]')),
    ('反斜杠目录串', re.compile('[\'"][^\'"\\n]*[\\\\/](?:Users|Desktop|Documents)[\\\\/]')),
    ('Unix 家目录路径', re.compile('[\'"]\\s*/(?:home|Users|mnt)/[A-Za-z0-9_.-]')),
    ('中文用户目录', re.compile('[\'"][^\'"\n]*(?:桌面|我的文档)[\\\\/]')),
    ('邮箱地址', re.compile('[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}')),
    ('私钥 / 令牌', re.compile('BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY|gh[pousr]_[A-Za-z0-9]{16,}')),
]

PRIVATE_PATTERNS_FILE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), ".leak-patterns.local")

SKIP_EXT = {'.o', '.a', '.lib', '.axf', '.bin', '.hex', '.pyc', '.woff2',
            '.jpg', '.jpeg', '.png', '.gif', '.mp4', '.pptx', '.xlsx', '.ttf'}


def load_private_patterns():
    """读取本地私有名单。缺失时明确提示，不静默跳过。"""
    name = os.path.basename(PRIVATE_PATTERNS_FILE)
    if not os.path.exists(PRIVATE_PATTERNS_FILE):
        print("  提示：未找到 %s，本次跳过姓名/机构类检查。" % name)
        print("        新建该文件、逐行写入正则即可启用。\n")
        return []
    out = []
    with open(PRIVATE_PATTERNS_FILE, encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, 1):
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            try:
                out.append(("私有名单 第%d条" % lineno, re.compile(raw)))
            except re.error as exc:
                print("  警告：%s 第 %d 行正则无效，已跳过（%s）" % (name, lineno, exc))
    print("  已载入 %s：%d 条私有模式\n" % (name, len(out)))
    return out


def decode(raw):
    for enc in ('utf-8', 'gb18030', 'latin-1'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return None


def list_files(root):
    """优先只列 git 追踪的文件——被 .gitignore 排除的文件推不出去，也就泄漏不了。"""
    import subprocess
    try:
        out = subprocess.run(['git', '-C', root, 'ls-files'],
                             capture_output=True, text=True, timeout=30)
        if out.returncode == 0 and out.stdout.strip():
            print('  范围：git 追踪的文件（未追踪与已忽略的不计）\n')
            return sorted(out.stdout.splitlines())
    except (OSError, subprocess.SubprocessError):
        pass
    print('  范围：整个目录树（此处不是 git 仓库）\n')
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ('.git', '__pycache__')]
        for fn in filenames:
            files.append(os.path.relpath(os.path.join(dirpath, fn), root))
    return sorted(files)


def scan(root, patterns):
    findings = {}
    scanned = skipped = 0
    for rel in list_files(root):
        if True:
            if os.path.splitext(rel)[1].lower() in SKIP_EXT:
                skipped += 1
                continue
            path = os.path.join(root, rel)
            try:
                with open(path, "rb") as fh:
                    text = decode(fh.read())
            except OSError:
                continue
            if text is None:
                skipped += 1
                continue
            scanned += 1
            for lineno, line in enumerate(text.splitlines(), 1):
                for name, pat in patterns:
                    if pat.search(line):
                        findings.setdefault(name, []).append(
                            (rel, lineno, line.strip()[:90]))
    return findings, scanned, skipped


def main(root):
    patterns = PATTERNS + load_private_patterns()
    findings, scanned, skipped = scan(root, patterns)
    print("扫描 %d 个文本文件，跳过 %d 个二进制/图片\n" % (scanned, skipped))
    clean = True
    for name, _ in patterns:
        rows = findings.get(name)
        if not rows:
            print("  OK   %s：无" % name)
            continue
        clean = False
        print("   !   %s：%d 处" % (name, len(rows)))
        for rel, lineno, val in rows[:10]:
            print("         %s:%d  %s" % (rel, lineno, val))
    print()
    print("结论：可以推送" if clean else "结论：有待处理项，先修掉再推")
    return 0 if clean else 1


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(here)
    sys.exit(main(target))
