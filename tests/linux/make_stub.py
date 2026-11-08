#!/usr/bin/env python3
"""Writes the stub image the Linux CI builds the runtime against (tests/linux/stub/generated/game): no recompiled
code, every symbol the host binds to absent (a trapping placeholder, as the recompiler makes for a symbol an image
lacks), the XDK layout and ABI rows kept. Run it on a game's generated folder when the SDK's symbol list changes:

    python sdk/tests/linux/make_stub.py generated/game sdk/tests/linux/stub/generated/game
"""
import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    lines = (src / "image_symbols.h").read_text().splitlines()
    names, keep = [], []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r"#define SYM_(\w+) ", line)
        if m:
            names.append(m.group(1))
        elif line.startswith("#define XDK_"):
            keep.append(line)
        elif line.startswith("#define IMAGE_ABI"):
            keep.append(line)
            while lines[i].endswith("\\"):
                i += 1
                keep.append(lines[i])
        i += 1
    absent = [0xFFFF0000 + k for k in range(len(names))]

    syms = ["#pragma once", "// stub image for the Linux CI (tests/linux/make_stub.py): every symbol absent",
            '#define IMAGE_XBE "default.xbe"']
    for name, a in zip(names, absent):
        syms += [f"#define SYM_{name} {a:08X}", f"#define HAS_{name} 0"]
    syms += keep

    shared = ["#pragma once", '#include "cpu/x86_ops.h"', ""]
    shared += [f"X86_EXTERN_FUNC(sub_{a:08X});" for a in absent]

    mapping = ['#include "x86_recomp_shared.h"', "",
               "const uint32_t X86CodeBase = 0x11000u;", "const uint32_t X86CodeEnd = 0x11000u;",
               "const uint32_t X86ImageBase = 0x10000u;", "const uint32_t X86ImageEnd = 0x12000u;",
               "const uint32_t X86EntryPoint = 0x11000u;", "const uint32_t X86KernelThunk = 0x11000u;", "",
               "const X86FuncMapping X86FuncMappings[] = {", "\t{0, nullptr},", "};", "",
               "const uint32_t X86Continuations[] = {", "\t0,", "};", ""]
    for a in absent:
        mapping += [f'X86_FUNC(__imp__sub_{a:08X}) {{ x86_trap(ctx, 0x{a:08X}u, "not in this image"); }}',
                    f"X86_WEAK_FUNC(sub_{a:08X}, __imp__sub_{a:08X})", ""]

    out.mkdir(parents=True, exist_ok=True)
    (out / "image_symbols.h").write_text("\n".join(syms) + "\n", newline="\n")
    (out / "x86_recomp_shared.h").write_text("\n".join(shared) + "\n", newline="\n")
    (out / "x86_func_mapping.cpp").write_text("\n".join(mapping), newline="\n")
    (out / "x86_recomp.0.cpp").write_text('#include "x86_recomp_shared.h"\n', newline="\n")
    print(f"{len(names)} symbols, {len(keep)} layout lines -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
