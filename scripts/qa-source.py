from pathlib import Path
import re
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
errors = []


def strip_cpp(text: str) -> str:
    out = []
    i = 0
    state = "code"
    raw_end = ""
    while i < len(text):
        c = text[i]
        d = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            # C++ raw strings: R"TAG(... )TAG"
            if c == "R" and d == '"':
                m = re.match(r'R"([^()]*)\(', text[i:])
                if m:
                    delim = m.group(1)
                    raw_end = ")" + delim + '"'
                    out.extend(" " * len(m.group(0)))
                    i += len(m.group(0)) - 1
                    state = "raw"
                else:
                    out.append(c)
            elif c == '"':
                state = "str"
                out.append(" ")
            elif c == "'":
                state = "char"
                out.append(" ")
            elif c == "/" and d == "/":
                state = "line"
                out.extend("  ")
                i += 1
            elif c == "/" and d == "*":
                state = "block"
                out.extend("  ")
                i += 1
            else:
                out.append(c)
        elif state == "raw":
            if text.startswith(raw_end, i):
                out.extend(" " * len(raw_end))
                i += len(raw_end) - 1
                state = "code"
            else:
                out.append("\n" if c == "\n" else " ")
        elif state == "str":
            if c == "\\":
                out.append(" ")
                if i + 1 < len(text):
                    i += 1
                    out.append(" ")
            elif c == '"':
                state = "code"
                out.append(" ")
            else:
                out.append("\n" if c == "\n" else " ")
        elif state == "char":
            if c == "\\":
                out.append(" ")
                if i + 1 < len(text):
                    i += 1
                    out.append(" ")
            elif c == "'":
                state = "code"
                out.append(" ")
            else:
                out.append("\n" if c == "\n" else " ")
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
        elif state == "block":
            if c == "*" and d == "/":
                state = "code"
                out.extend("  ")
                i += 1
            else:
                out.append("\n" if c == "\n" else " ")
        i += 1
    return "".join(out)


for path in list(SRC.glob("*.cpp")) + list(SRC.glob("*.hpp")):
    stripped = strip_cpp(path.read_text(encoding="utf-8"))
    for opener, closer, label in [("{", "}", "braces"), ("(", ")", "parentheses"), ("[", "]", "brackets")]:
        depth = 0
        for ch in stripped:
            if ch == opener:
                depth += 1
            elif ch == closer:
                depth -= 1
                if depth < 0:
                    errors.append(f"{path.name}: premature closing {label}")
                    break
        if depth != 0:
            errors.append(f"{path.name}: unbalanced {label} ({depth})")

    for include in re.findall(r'#include\s+"([^"]+)"', path.read_text(encoding="utf-8")):
        if not (SRC / include).exists():
            errors.append(f"{path.name}: missing local include {include}")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
for path in SRC.glob("*.cpp"):
    if f"src/{path.name}" not in cmake:
        errors.append(f"CMakeLists.txt does not include {path.name}")

if (ROOT / "VERSION").read_text(encoding="utf-8").strip() != "0.4.0":
    errors.append("VERSION is not 0.4.0")

workflow_path = ROOT / ".github" / "workflows" / "build-windows.yml"
try:
    workflow = yaml.safe_load(workflow_path.read_text(encoding="utf-8"))
    if not isinstance(workflow, dict) or "jobs" not in workflow:
        errors.append("GitHub Actions workflow has no jobs")
except Exception as exc:
    errors.append(f"Invalid workflow YAML: {exc}")

required = [
    ROOT / "docs" / "API_SETUP.md",
    ROOT / "docs" / "OAUTH_PRODUCTION.md",
    ROOT / "docs" / "BUILD_WINDOWS.md",
    ROOT / "docs" / "INSTALLER_WINDOWS.md",
    ROOT / "installer" / "social-comments.iss",
    ROOT / "broker" / "server.mjs",
    ROOT / "broker" / "README.md",
    ROOT / "CHANGELOG.md",
]
for path in required:
    if not path.exists():
        errors.append(f"Missing {path.relative_to(ROOT)}")

print(f"Source files checked: {len(list(SRC.glob('*.cpp')))} cpp + {len(list(SRC.glob('*.hpp')))} hpp")
if errors:
    print("QA FAIL")
    for error in errors:
        print(" -", error)
    sys.exit(1)
print("QA PASS")
