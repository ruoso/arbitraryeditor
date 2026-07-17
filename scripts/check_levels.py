#!/usr/bin/env python3
"""Levelization check for the editor (docs/01-architecture.md §8).

Two invariants over the directory-per-component layout under src/:

  1. Cross-component includes `#include <ace/<other>/...>` resolve only within
     the transitive closure of a component's declared direct dependencies.
  2. The testability seam (A8): only the ImGui layer (views/dock/app) may
     include ImGui; only the GL-facing components may include GL; only the app
     may include SDL. The whole L1 core stays UI-agnostic by construction.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT / "src"

# Direct dependencies per component — the §8 DAG (the source of truth).
ALLOWED = {
    "base": set(),
    "platform": {"base"},
    "gl": {"base"},
    "project": {"base", "platform"},
    "scene": {"base", "project"},
    "interact": {"base", "scene"},
    "commands": {"base", "project", "scene"},
    "dockmodel": {"base", "platform"},
    "render": {"base", "project", "scene", "gl"},
    "views": {"scene", "interact", "commands", "render", "dockmodel"},
    "dock": {"dockmodel", "views"},
    "app": {
        "base", "platform", "gl", "project", "scene", "interact",
        "commands", "dockmodel", "render", "views", "dock",
    },
}

# The seam (A8): which components may include which external UI stacks.
EXTERNAL_ALLOWED = {
    "imgui": {"views", "dock", "app"},
    "sdl": {"app"},
    "gl_api": {"gl", "render", "views", "dock", "app"},
}
EXTERNAL_RE = {
    "imgui": re.compile(r'#\s*include\s*[<"]imgui'),
    "sdl": re.compile(r'#\s*include\s*[<"]SDL'),
    "gl_api": re.compile(r'#\s*include\s*[<"](GL/|GLES|glad|SDL_opengl)'),
}
ACE_INCLUDE_RE = re.compile(r'#\s*include\s*<ace/([a-z_]+)/')


def closure(component):
    seen, stack = set(), list(ALLOWED.get(component, set()))
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        stack.extend(ALLOWED.get(c, set()))
    return seen


def main():
    violations = []
    for comp_dir in sorted(p for p in SRC.iterdir() if p.is_dir()):
        comp = comp_dir.name
        if comp not in ALLOWED:
            violations.append(f"{comp}: component not in the §8 level DAG (ALLOWED)")
            continue
        allowed_closure = closure(comp)
        for src in sorted(comp_dir.rglob("*")):
            if src.suffix not in {".hpp", ".cpp", ".h", ".cc"}:
                continue
            text = src.read_text(encoding="utf-8", errors="replace")
            rel = src.relative_to(REPO_ROOT)
            for other in ACE_INCLUDE_RE.findall(text):
                if other != comp and other not in allowed_closure:
                    violations.append(
                        f"{rel}: includes <ace/{other}/...> — not in {comp}'s dependency closure"
                    )
            for ext, rx in EXTERNAL_RE.items():
                if rx.search(text) and comp not in EXTERNAL_ALLOWED[ext]:
                    violations.append(
                        f"{rel}: includes {ext} — forbidden in '{comp}' (testability seam, A8)"
                    )

    if violations:
        print("check_levels: FAIL")
        for v in violations:
            print(f"  {v}")
        return 1
    print(f"check_levels: OK ({len(ALLOWED)} components)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
