#!/usr/bin/env python3
"""Static checks for the classic-ESP32 OLED build, for when there is no ESP32 compiler.

The ESP32 toolchain cannot be downloaded in this sandbox, so a real Arduino/PlatformIO build
is not possible here. What IS possible is answering the questions a build would answer about
OUR changes, which is exactly what "not yet verified: MenuFunctions.cpp" means:

  1. Does every macro in an `#if` actually exist? A typo (`MARAUDER_POOMM`) does not fail a
     build, it silently deletes code - the worst kind of firmware bug.
  2. Are `#if`/`#elif`/`#endif` balanced in every file?
  3. Which branches are live for this board, and does every `tft.<method>()` call in a live
     branch exist on PoomDisplay? TFT_eSPI has ~200 methods; PoomDisplay has the ~45 this
     board needs. A call to a method we did not implement is a compile error on the bench.
  4. Does `configs.h` alone (no -D flags, like the Arduino IDE) resolve to the board, the
     pins and the feature flags the notes promise?
  5. Does partitions.csv add up: 4 MB flash, no overlaps, room for the log next to settings?

Usage: python3 firmware/tools/check_sources.py [--board MARAUDER_ESP32_OLED]
Exit code 0 = no errors. Warnings are printed but do not fail the check.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)
SRC = os.path.join(FIRMWARE, "upstream", "esp32_marauder")
STUBS = os.path.join(FIRMWARE, "host_tests", "stubs")

# Macro names whose spelling decides whether code is in or out of the build.
BOARD_MACRO_RE = re.compile(r"^(MARAUDER_|HAS_|USE_|BOARD_|DUAL_|GENERIC_|XIAO_ESP32|ESP32_LDDB)")
TFT_CALL_RE = re.compile(r"\btft\s*\.\s*([A-Za-z_]\w*)\s*\(")
DIRECTIVE_RE = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif|define|undef|include)\b(.*)$")
POOM_OK_FILES = {"configs.h", "LedInterface.h", "LedInterface.cpp", "PoomWs2812.h",
                 "PoomWs2812.cpp", "PoomDisplay.h", "PoomDisplay.cpp"}
LIVE, DEAD, UNKNOWN = "live", "dead", "unknown"


class Result:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, msg: str) -> None:
        self.errors.append(msg)

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)


# ------------------------------------------------------------------ board macro set
def preprocess_board_macros(board: str) -> tuple[set[str] | None, str]:
    """The macro names configs.h defines for this board, taken from the real preprocessor."""
    probe = '#include "configs.h"\n#define MARAUDER_PROBE_SENTINEL 1\n'
    fd, path = tempfile.mkstemp(suffix=".cpp")
    with os.fdopen(fd, "w") as fh:
        fh.write(probe)
    cmd = ["g++", "-std=c++17", "-E", "-dD", f"-D{board}", f"-I{STUBS}", f"-I{SRC}", path]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        os.unlink(path)
        return None, str(exc)
    os.unlink(path)
    if out.returncode != 0:
        return None, out.stderr.strip().splitlines()[-1] if out.stderr else "preprocess failed"
    names = set()
    for line in out.stdout.splitlines():
        m = re.match(r"^#define\s+([A-Za-z_]\w*)", line)
        if m:
            names.add(m.group(1))
    names.discard("MARAUDER_PROBE_SENTINEL")
    names.add(board)
    # g++'s own defines (unix, __cplusplus, ...) are noise; only keep board-shaped ones plus
    # everything configs.h mentions, which the caller assembles separately.
    return names, ""


def mentioned_names(path: str) -> set[str]:
    """Identifiers configs.h mentions in guards or defines: a name here is not a typo."""
    names = set()
    text = read(path)
    for m in re.finditer(r"#\s*(?:if|ifdef|ifndef|elif)\b(.*)", text):
        names.update(re.findall(r"[A-Za-z_]\w*", m.group(1)))
    for m in re.finditer(r"#\s*define\s+([A-Za-z_]\w*)", text):
        names.add(m.group(1))
    names.update(("defined", "true", "false"))
    return names


def read_raw(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def read(path: str) -> str:
    return strip_comments(read_raw(path))


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, keeping newlines so line numbers stay true.

    Needed because upstream has commented-out blocks that contain #ifdef/#endif, and counting
    those is how a lint tool ends up crying wolf about an unbalanced file.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
            continue
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out.append(text[i])
                    i += 1
                    if i < n:
                        out.append(text[i])
                        i += 1
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(text[i])
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


# Headers whose symbols must be visible in whatever file uses them. Arduino IDE compiles
# each .cpp on its own, so a missing include here is a compile error on the bench, not
# something the .ino can rescue. (This check found exactly that: changeMenu() used
# slideDirection() from OledSlide.h without MenuFunctions.cpp including it.)
TRACKED_HEADERS = ["OledAnim.h", "OledSlide.h", "RotatingLog.h", "LogStores.h", "PoomDisplay.h"]


def header_symbols(path: str) -> set[str]:
    """Free functions, constants and class names a header hands to its includers.

    Only depth-0 declarations count: class members (`write`, `trim`, ...) would otherwise
    match unrelated code everywhere and the check would cry wolf.
    """
    names = set()
    depth = 0
    skip = {"if", "for", "while", "return", "sizeof", "defined", "switch", "catch"}
    for line in read(path).splitlines():
        stripped = line.strip()
        if depth == 0 and stripped and not stripped.startswith(("//", "*", "#", "public", "private", "protected")):
            m = re.search(r"\b(?:class|struct)\s+([A-Za-z_]\w*)", stripped)
            if m:
                names.add(m.group(1))
            m = re.search(r"\bconstexpr\s+[\w:]+\s+([A-Za-z_]\w*)\s*=", stripped)
            if m:
                names.add(m.group(1))
            m = re.match(r"[A-Za-z_][\w :*&<>,.]*?\b([A-Za-z_]\w*)\s*\(", stripped)
            if m and m.group(1) not in skip:
                names.add(m.group(1))
        depth += line.count("{") - line.count("}")
        depth = max(depth, 0)
    return names


def include_graph(files) -> dict:
    """file -> set of local headers it #includes, so 'visible' can follow one hop or ten."""
    graph = {}
    for path in files:
        rel = os.path.relpath(path, SRC)
        incs = set()
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', read_raw(path), re.M):
            incs.add(os.path.normpath(os.path.join(os.path.dirname(rel), m.group(1))))
        graph[rel] = incs
    return graph


def visible_headers(graph: dict, rel: str) -> set[str]:
    seen: set[str] = set()
    queue = [rel]
    while queue:
        cur = queue.pop()
        for inc in graph.get(cur, set()):
            if inc not in seen:
                seen.add(inc)
                queue.append(inc)
    return seen


def iter_source_files():
    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d not in (".theia", "data")]
        for f in sorted(files):
            if f.endswith((".cpp", ".h", ".ino")):
                yield os.path.join(root, f)


# ------------------------------------------------------------------ condition parser
class Cond:
    """A tiny evaluator for board guards: defined(X), !defined X, X || Y, X && Z, 1/0.

    Returns LIVE / DEAD / UNKNOWN. Anything it cannot express is UNKNOWN, and UNKNOWN is
    treated as 'this branch is compiled' so the checks never go quiet by accident.
    """

    def __init__(self, live_names: set[str]) -> None:
        self.names = live_names

    def eval(self, expr: str) -> str:
        expr = expr.replace("\\" + "\n", " ")
        parts_or = re.split(r"\|\|", expr)
        if len(parts_or) > 1:
            results = [self.eval(p) for p in parts_or]
            if LIVE in results:
                return LIVE
            return DEAD if all(r == DEAD for r in results) else UNKNOWN
        parts_and = re.split(r"&&", expr)
        if len(parts_and) > 1:
            results = [self.eval(p) for p in parts_and]
            if DEAD in results:
                return DEAD
            return LIVE if all(r == LIVE for r in results) else UNKNOWN
        term = expr.strip()
        if not term:
            return UNKNOWN
        negate = False
        if term.startswith("!"):
            negate = True
            term = term[1:].strip()
        m = re.fullmatch(r"defined\s*\(?\s*([A-Za-z_]\w*)\s*\)?", term)
        if not m:
            m = re.fullmatch(r"([A-Za-z_]\w*)", term)
            if m and m.group(1) in ("true", "false"):
                return LIVE if (m.group(1) == "true") != negate else DEAD
        if m:
            name = m.group(1)
            known = name in self.names
            state = LIVE if known else DEAD
            return DEAD if negate else state
        if re.fullmatch(r"[0-9]+", term):
            return LIVE if int(term) != 0 else DEAD
        if re.fullmatch(r"0[xX][0-9a-fA-F]+", term):
            return LIVE if int(term, 0) != 0 else DEAD
        # ARDUINO_ESP32_GITHUB_COMMIT, SOC_x_Y == 1, defined(X) && defined(Y) ...
        return UNKNOWN


# ------------------------------------------------------------------ per-file scanner
class Scanner:
    def __init__(self, board: str, macros: set[str], known_names: set[str], res: Result) -> None:
        self.board = board
        self.macros = macros
        self.known_names = known_names
        self.res = res
        self.cond = Cond(macros)

    def check_condition_names(self, expr: str, where: str) -> None:
        for name in re.findall(r"[A-Za-z_]\w*", expr):
            if not BOARD_MACRO_RE.match(name):
                continue
            if name in self.macros or name in self.known_names:
                continue
            self.res.error(f"{where}: guard mentions {name}, which configs.h never defines "
                           f"for any board (typo -> the code below it is compiled out)")

    def check_file(self, path: str) -> dict[str, int]:
        rel = os.path.relpath(path, SRC)
        lines = read(path).splitlines()
        stack: list[dict] = []
        local: set[str] = set()
        methods: dict[str, int] = {}
        live_ifdef_poom = []

        def parent_live() -> bool:
            # UNKNOWN counts as compiled: a region we cannot evaluate is checked, not skipped.
            return all(f["live"] != DEAD for f in stack)

        for no, raw in enumerate(lines, 1):
            m = DIRECTIVE_RE.match(raw)
            if m:
                kind, rest = m.group(1), m.group(2).strip()
                if kind in ("include", "define", "undef"):
                    if kind == "define":
                        md = re.match(r"([A-Za-z_]\w*)", rest)
                        if md:
                            local.add(md.group(1))
                    elif kind == "undef":
                        md = re.match(r"([A-Za-z_]\w*)", rest)
                        if md:
                            local.discard(md.group(1))
                    continue
                names = self.macros | local
                if kind in ("ifdef", "ifndef"):
                    tok = rest.split()[0] if rest.split() else ""
                    if BOARD_MACRO_RE.match(tok) and tok not in names and tok not in self.known_names:
                        self.res.error(f"{rel}:{no}: '{kind} {tok}' - {tok} is not defined by "
                                       f"any board config (typo?)")
                    state = (tok in names)
                    if kind == "ifndef":
                        state = not state
                    branch = LIVE if (parent_live() and state) else DEAD
                    stack.append({"live": branch, "any": LIVE if state else DEAD})
                    continue
                if kind in ("if", "elif"):
                    cond = self.cond.eval(rest) if parent_live() else DEAD
                    if cond != DEAD:
                        self.check_condition_names(rest, f"{rel}:{no}")
                    if kind == "if":
                        branch = cond if (parent_live() and cond != DEAD) else DEAD
                        stack.append({"live": branch, "any": branch})
                    else:
                        if not stack:
                            self.res.error(f"{rel}:{no}: '#elif' without '#if'")
                            continue
                        frame = stack[-1]
                        if frame["any"] == LIVE or not parent_live():
                            frame["live"] = DEAD
                        else:
                            frame["live"] = cond
                            frame["any"] = cond
                    continue
                if kind == "else":
                    if not stack:
                        self.res.error(f"{rel}:{no}: '#else' without '#if'")
                        continue
                    frame = stack[-1]
                    frame["live"] = DEAD if (frame["any"] == LIVE or not parent_live()) else LIVE
                    if frame["any"] != LIVE:
                        frame["any"] = frame["live"]
                    continue
                if kind == "endif":
                    if not stack:
                        self.res.error(f"{rel}:{no}: '#endif' with no open '#if'")
                    else:
                        stack.pop()
                    continue

            if parent_live():
                for meth in TFT_CALL_RE.findall(raw):
                    methods.setdefault(meth, no)
                if "MARAUDER_POOM" in raw and rel not in POOM_OK_FILES:
                    live_ifdef_poom.append(no)
        for no in live_ifdef_poom:
            self.res.warn(f"{rel}:{no}: live code tests MARAUDER_POOM; the OLED paths use "
                          f"MARAUDER_OLED128X64, so check this is not meant for both boards")
        if stack:
            self.res.error(f"{rel}: {len(stack)} '#if' left open at end of file")
        return methods


# ------------------------------------------------------------------ PoomDisplay API
def display_api(path: str) -> set[str]:
    text = read(path)
    m = re.search(r"class PoomDisplay[^{]*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("could not find class PoomDisplay in " + path)
    names = set()
    for line in m.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith(("//", "*", "#", "public:", "private:", "protected:")):
            continue
        mm = re.search(r"\b([A-Za-z_]\w*)\s*\(", line)
        if mm:
            names.add(mm.group(1))
    return names | {"print", "println", "printf", "write"}   # Print base class


# ------------------------------------------------------------------ partitions.csv
def check_partitions(path: str, res: Result, log_cap: int) -> tuple[int, int]:
    if not os.path.exists(path):
        res.warn(f"{os.path.relpath(path, FIRMWARE)} not found; skipped the partition checks")
        return 0, 0
    text = read(path)
    parts = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [f.strip() for f in line.split(",")]
        if len(fields) < 5:
            continue
        try:
            offset = int(fields[3], 0)
            size = int(fields[4], 0)
        except ValueError:
            res.error(f"partitions.csv: '{line}' has a non-numeric offset or size")
            continue
        parts.append((fields[0], fields[1], fields[2], offset, size))
    flash = 4 * 1024 * 1024
    app = spiffs = 0
    for i, (name, ptype, subtype, offset, size) in enumerate(parts):
        if offset + size > flash:
            res.error(f"partitions.csv: {name} ends at {offset + size}, past 4 MB flash")
        if i and offset < parts[i - 1][3] + parts[i - 1][4]:
            res.error(f"partitions.csv: {name} overlaps {parts[i - 1][0]}")
        if size % 4096:
            res.error(f"partitions.csv: {name} size {size} is not a multiple of the 4 KB flash sector")
        if ptype == "app":
            app = max(app, size)
        if subtype == "spiffs":
            spiffs = size
    if not app:
        res.error("partitions.csv: no app partition")
    if not spiffs:
        res.error("partitions.csv: no spiffs partition (settings live there)")
    else:
        headroom = spiffs - log_cap
        if headroom < 64 * 1024:
            res.error(f"partitions.csv: spiffs has only {headroom // 1024} KB beyond the "
                      f"{log_cap // 1024} KB log; SPIFFS needs free blocks to wear-level")
    return app, spiffs


def check_single_board_selection(text: str, board: str, res: Result) -> None:
    """configs.h picks a board with one #define between the BOARD TARGETS markers."""
    try:
        block = text.split("//// BOARD TARGETS", 1)[1].split("//// END BOARD TARGETS", 1)[0]
    except IndexError:
        res.error("configs.h: could not find the BOARD TARGETS block")
        return
    chosen = [m.group(1) for m in re.finditer(r"^\s*#\s*define\s+(MARAUDER_\w+|GENERIC_ESP32|"
                                              r"XIAO_ESP32_\w+|ESP32_LDDB|DUAL_MINI_C5)\b",
                                              block, re.M)]
    if not chosen:
        res.error("configs.h: no board target is enabled - the build has no pins at all")
    elif len(chosen) > 1:
        res.error(f"configs.h: {len(chosen)} board targets enabled at once ({', '.join(chosen)}); "
                  f"one board per build, or the pin maps collide")
    elif chosen[0] != board:
        res.error(f"configs.h selects {chosen[0]}, not {board}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", default="MARAUDER_ESP32_OLED")
    args = ap.parse_args()
    res = Result()

    if not os.path.isdir(SRC):
        print("cannot find", SRC, file=sys.stderr)
        return 2

    macros, err = preprocess_board_macros(args.board)
    if macros is None:
        res.error(f"could not preprocess configs.h ({err}). The pin/feature checks below were "
                  f"skipped; install g++ and run from any directory.")
        macros = set()
    for needed in (args.board, "HAS_SCREEN", "MARAUDER_OLED128X64"):
        if macros and needed not in macros:
            res.error(f"configs.h does not resolve to {needed}: the build would not select this board")

    configs_text = read_raw(os.path.join(SRC, "configs.h"))
    check_single_board_selection(configs_text, args.board, res)
    known = mentioned_names(os.path.join(SRC, "configs.h"))
    api = display_api(os.path.join(SRC, "PoomDisplay.h"))
    scanner = Scanner(args.board, macros, known, res)

    files = list(iter_source_files())
    graph = include_graph(files)
    tracked = {}
    for hdr in TRACKED_HEADERS:
        hp = os.path.join(SRC, hdr)
        if os.path.exists(hp):
            tracked[hdr] = header_symbols(hp)

    missing: dict[str, tuple[str, int]] = {}
    for path in files:
        methods = scanner.check_file(path)
        for meth, line in methods.items():
            if meth not in api:
                missing.setdefault(meth, (os.path.relpath(path, SRC), line))
    for meth, (where, line) in sorted(missing.items()):
        res.error(f"{where}:{line}: tft.{meth}() is live for {args.board} but PoomDisplay has no "
                  f"{meth}() (TFT_eSPI does; this board will not compile)")

    # Missing includes for the headers this port added.
    for path in files:
        rel = os.path.relpath(path, SRC)
        if not rel.endswith((".cpp", ".ino")):
            continue
        have = visible_headers(graph, rel)
        text = read(path)
        for hdr, syms in tracked.items():
            if hdr in have:
                continue
            used = sorted(s for s in syms if re.search(r"\b" + re.escape(s) + r"\b", text))
            if used:
                line = next(i for i, l in enumerate(text.splitlines(), 1)
                            if any(re.search(r"\b" + re.escape(u) + r"\b", l) for u in used))
                res.error(f"{rel}:{line}: uses {', '.join(used[:4])} from {hdr} but neither it "
                          f"nor anything it includes has the include for it")

    app, spiffs = check_partitions(os.path.join(SRC, "partitions.csv"), res, 256 * 1024)
    if app and app < 2560 * 1024:
        res.error(f"partitions.csv: app partition is {app // 1024} KB; upstream builds are ~2.7 MB")

    print(f"checked {len(files)} files, {len(macros)} board macros, "
          f"{len(api)} PoomDisplay methods, app {app // 1024} KB / spiffs {spiffs // 1024} KB")
    for w in res.warnings:
        print("warn :", w)
    for e in res.errors:
        print("ERROR:", e)
    if res.errors:
        print(f"{len(res.errors)} error(s), {len(res.warnings)} warning(s)")
        return 1
    print(f"no errors ({len(res.warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
