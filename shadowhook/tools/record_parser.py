#!/usr/bin/env python3

# Copyright (c) 2021-2026 ByteDance Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# Created by Kelun Cai (caikelun@bytedance.com) on 2026-04-23.

import os
import sys
import argparse
import bisect
try:
    from capstone import *
    from capstone.arm64 import *
    from capstone.arm import *
except ImportError:
    print("Error: missing dependency 'capstone'.")
    print("Install it using:")
    print(f"    {sys.executable} -m pip install capstone")
    sys.exit(1)


g_more_info = False
g_maps = []
g_maps_starts = []

BASE_SRC_URL = 'shadowhook/src/main/cpp'
LOGCAT_TAG = 'shadowhook_tag:'
ITEM_COUNT = 12
ITEM_OP_IDX = 9


def parse_maps(filepath):
    global g_maps, g_maps_starts
    entries = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(None, 5)
            if len(parts) < 5:
                continue
            addr_range = parts[0]
            start_str, end_str = addr_range.split('-')
            if len(parts) >= 6:
                pathname = parts[5]
            else:
                pathname = f"<anonymous:{start_str}-{end_str}>"
            entries.append((int(start_str, 16), int(end_str, 16), pathname))
    entries.sort(key=lambda e: e[0])
    g_maps = entries
    g_maps_starts = [e[0] for e in entries]


def lookup_addr(addr):
    if not g_maps:
        return None
    idx = bisect.bisect_right(g_maps_starts, addr) - 1
    if idx < 0:
        return None
    start, end, pathname = g_maps[idx]
    if start <= addr < end:
        return pathname
    return None


def parse_instr(code_str, base_str, arch):
    code = bytes.fromhex(code_str)
    base = int(base_str, 16)
    base &= ~1

    if arch == 'arm64':
        md          = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        ptr_size    = 8
        addr_width  = 16
        hex_col     = 16
        data_dir    = ".quad"
    elif arch == 'arm':
        md          = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        ptr_size    = 4
        addr_width  = 8
        hex_col     = 8
        data_dir    = ".word"
    elif arch == 'thumb':
        md          = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        ptr_size    = 4
        addr_width  = 8
        hex_col     = 8
        data_dir    = ".word"
    else:
        raise ValueError(f"Unsupported architecture: {arch}")

    md.detail = True
    md.skipdata = True

    literal_addrs = set()
    for insn in md.disasm(code, base):
        if arch == 'arm64':
            # ARM64 LDR literal: second operand is an immediate (absolute addr)
            if insn.id == ARM64_INS_LDR:
                ops = insn.operands
                if len(ops) == 2 and ops[1].type == ARM64_OP_IMM:
                    target = ops[1].imm
                    literal_addrs.add(target)
        else:  # arm / thumb
            # ARM/Thumb LDR literal: second operand is [PC, #disp]
            if insn.id == ARM_INS_LDR:
                ops = insn.operands
                if len(ops) == 2 and ops[1].type == ARM_OP_MEM:
                    mem = ops[1].mem
                    if mem.base == ARM_REG_PC and mem.index == ARM_REG_INVALID:
                        if arch == 'thumb':
                            # Thumb: PC = Align(insn_addr + 4, 4)
                            pc_val = (insn.address + 4) & ~3
                        else:
                            # ARM:   PC = insn_addr + 8
                            pc_val = insn.address + 8
                        disp = mem.disp
                        # capstone may expose an unsigned disp + subtracted flag
                        if getattr(ops[1], 'subtracted', False):
                            disp = -disp
                        literal_addrs.add(pc_val + disp)

    offset = 0
    size = len(code)
    while offset < size:
        cur_addr = base + offset
        # literal pool entry (data-in-code)
        if cur_addr in literal_addrs and offset + ptr_size <= size:
            data = code[offset:offset + ptr_size]
            val  = int.from_bytes(data, "little")
            print(f"        0x{cur_addr:0{addr_width}x}:  "
                  f"{data.hex():<{hex_col}}  "
                  f"{data_dir:<10} 0x{val:x}")
            offset += ptr_size
            continue
        # normal instruction
        insns = list(md.disasm(code[offset:], cur_addr, count=1))
        if insns:
            insn = insns[0]
            print(f"        0x{insn.address:0{addr_width}x}:  "
                  f"{insn.bytes.hex():<{hex_col}}  "
                  f"{insn.mnemonic:<10} {insn.op_str}")
            offset += insn.size
        else:
            # bytes that Capstone cannot decode even with skipdata
            step = 2 if arch == 'thumb' else 4
            raw  = code[offset:offset + step]
            print(f"        0x{cur_addr:0{addr_width}x}:  "
                  f"{raw.hex():<{hex_col}}  "
                  f"{'undefined':<10} "
                  f"{', '.join(f'0x{b:02x}' for b in raw)}")
            offset += len(raw)


def parse_memory_type(t):
    return 'ANON PAGE' if t == '0' else 'ELF GAP'


def find_val(items, tag):
    for item in items:
        if item.startswith(tag + '|'):
            return item.split("|")[1:]
    return None


def parse_trace(trace):
    global g_more_info

    items = trace.split(";")

    arch = ''
    hub_addr = ''
    for item in items:
        if item.startswith('H|'):
            hub_addr = item.split("|")[1]
    
    v = find_val(items, 'S')
    if v:
        print(f"  * ELF info")
        print(f"    * load bias :  0x{v[0]}")
        load_bias = int(v[0], 16)
        for gap in v[1:]:
            r = gap.split("-")
            start = load_bias + int(r[0], 16)
            end = load_bias + int(r[1], 16)
            print(f"    * gap       : [{start:#x}, {end:#x}) , [0x{r[0]}, 0x{r[1]}) , size {end - start}")
    v = find_val(items, 'B')
    if v:
        print(f"  * hook info")
        print(f"    * arch : {v[0]}")
        print(f"    * op   : {v[1]}")
        arch = v[0]
    v = find_val(items, 'T')
    if v:
        print(f"  * target")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : {arch}")
        print(f"    * length  : {len(v[1]) // 2}")
        print(f"    * original instr")
        parse_instr(v[1], v[0], arch)
        if arch == 'thumb': print(f"    * length  : {len(v[2]) // 2}")
        print(f"    * new instr")
        parse_instr(v[2], v[0], arch)
    v = find_val(items, 'X')
    if v:
        print(f"  * island exit")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : {arch}")
        print(f"    * type    : {parse_memory_type(v[1])}")
        print(f"    * length  : {len(v[2]) // 2}")
        print(f"    * instr")
        parse_instr(v[2], v[0], arch)
    v = find_val(items, 'N')
    if v:
        print(f"  * new")
        print(f"    * address : 0x{v[0]}")
    v = find_val(items, 'L')
    if v:
        print(f"  * glue launcher")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : arm")
        print(f"    * length  : {len(v[1]) // 2}")
        print(f"    * instr")
        parse_instr(v[1], v[0], 'arm64' if arch == 'arm64' else 'arm')
        if g_more_info:
            print(f"    * symbol  : sh_inst_build_glue_launcher")
            print(f"    * file    : {BASE_SRC_URL}/arch/{'arm64' if arch == 'arm64' else 'arm'}/sh_inst.c")
    v = find_val(items, 'G')
    if v:
        print(f"  * glue")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : arm")
        if g_more_info:
            print(f"    * symbol  : shadowhook_interceptor_glue")
            print(f"    * file    : {BASE_SRC_URL}/arch/{'arm64' if arch == 'arm64' else 'arm'}/sh_glue.S")
    v = find_val(items, 'I')
    if v:
        print(f"  * interceptor list")
        for f in v: print(f"    * function : 0x{f}")
        if g_more_info:
            print(f"    * symbol   : sh_switch_t.interceptors")
            print(f"    * file     : {BASE_SRC_URL}/sh_switch.c")
    v = find_val(items, 'Q')
    if v:
        print(f"  * proxy list")
        for f in v: print(f"    * {'hub     ' if f == hub_addr else 'function'} : 0x{f}")
        if g_more_info:
            print(f"    * symbol   : sh_switch_t.proxies")
            print(f"    * file     : {BASE_SRC_URL}/sh_switch.c")
    v = find_val(items, 'H')
    if v:
        print(f"  * proxy hub")
        print(f"    * address  : 0x{v[0]}")
        print(f"    * original : 0x{v[1]}")
        for f in v[2:]: print(f"    * function : 0x{f}")
        if g_more_info:
            print(f"    * symbol   : sh_hub_trampo_template")
            print(f"    * file     : {BASE_SRC_URL}/sh_hub.c")
    v = find_val(items, 'U')
    if v:
        print(f"  * unique proxy")
        print(f"    * function : 0x{v[0]}")
        if g_more_info:
            print(f"    * symbol   : sh_switch_t.proxy_addr")
            print(f"    * file     : {BASE_SRC_URL}/sh_switch.c")
    v = find_val(items, 'E')
    if v:
        print(f"  * enter")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : {arch}")
        print(f"    * length  : {len(v[1]) // 2}")
        print(f"    * instr")
        parse_instr(v[1], v[0], arch)
    v = find_val(items, 'W')
    if v:
        print(f"  * island rewrite")
        print(f"    * address : 0x{v[0]}")
        print(f"    * type    : {parse_memory_type(v[1])}")
        print(f"    * length  : {len(v[2]) // 2}")
        print(f"    * instr")
        parse_instr(v[2], v[0], arch)
    v = find_val(items, 'e')
    if v:
        print(f"  * island enter")
        print(f"    * address : 0x{v[0]}")
        print(f"    * type    : {parse_memory_type(v[1])}")
        print(f"    * length  : {len(v[2]) // 2}")
        print(f"    * instr")
        parse_instr(v[2], v[0], arch)
    v = find_val(items, 'R')
    if v:
        print(f"  * resume")
        print(f"    * address : 0x{v[0]}")
        if arch != 'arm64': print(f"    * arch    : {arch}")


def parse_flags(flags, op):
    flags_str = ''
    n = int(flags)
    if op.startswith("hook_"):
        if n & 0b0111 == 0: flags_str += 'default_mode | '
        if n & 0b0001: flags_str += 'shared_mode | '
        if n & 0b0010: flags_str += 'unique_mode | '
        if n & 0b0100: flags_str += 'multi_mode | '
        if n & 0b1000: flags_str += 'record | '
    elif op.startswith("intercept_"):
        if n & 0b011 == 0: flags_str += 'default | '
        if n & 0b011 == 1: flags_str += 'read_only | '
        if n & 0b011 == 2: flags_str += 'write_only | '
        if n & 0b011 == 3: flags_str += 'read_write | '
        if n & 0b100: flags_str += 'record | '
    return flags if not flags_str else (flags + ' (' + flags_str[:-3] + ')')


def op_is_unop(op):
    return op == 'unhook' or op == 'unintercept'


def check_flags(flags, idx):
    return (flags >> idx) & 1


def parse_line(line, flags):
    item_ts = None
    item_caller = None
    item_op = None
    item_target_lib = None   # op only
    item_target_sym = None   # op only
    item_target_addr = None  # op only
    item_new_addr = None     # op only
    item_backup_len = None   # op only
    item_errno = None
    item_stub = None
    item_flags = None        # op only
    item_trace = None

    unop_mask = 0b101100000111

    op_flags = int(flags, 2)
    unop_flags = op_flags & unop_mask

    i = -1
    for item in line.split(","):
        i += 1
        while i < ITEM_COUNT and not check_flags(unop_flags if op_is_unop(item_op) else op_flags, i):
            i += 1
        if i >= ITEM_COUNT:
            raise ValueError(f"format error: {flags}")
        elif i == 0:
            item_ts = item
        elif i == 1:
            item_caller = item
        elif i == 2:
            item_op = item
        elif i == 3:
            item_target_lib = item
        elif i == 4:
            item_target_sym = item
        elif i == 5:
            item_target_addr = item
        elif i == 6:
            item_new_addr = item
        elif i == 7:
            item_backup_len = item
        elif i == 8:
            item_errno = item
        elif i == 9:
            item_stub = item
        elif i == 10:
            item_flags = item
        elif i == 11:
            item_trace = item

    if item_flags is not None:
        item_flags = parse_flags(item_flags, item_op)

    if item_target_lib == 'unknown' and item_target_addr is not None:
        pathname = lookup_addr(int(item_target_addr, 16))
        if pathname is not None:
            item_target_lib = pathname + ' (found in /proc/self/maps)'

    if item_ts is not None:
        print(f"* timestamp            : {item_ts}")
    if item_caller is not None:
        print(f"* caller library name  : {item_caller}")
    if item_op is not None:
        print(f"* operation type       : {item_op}")
    if item_target_lib is not None:
        print(f"* target library name  : {item_target_lib}")
    if item_target_sym is not None:
        print(f"* target symbol name   : {item_target_sym}")
    if item_target_addr is not None:
        print(f"* target address       : 0x{item_target_addr}")
    if item_new_addr is not None:
        print(f"* new address          : 0x{item_new_addr}")
    if item_backup_len is not None:
        print(f"* backup length        : {item_backup_len}")
    if item_errno is not None:
        print(f"* errno                : {item_errno}")
    if item_stub is not None:
        print(f"* stub                 : 0x{item_stub}")
    if item_flags is not None:
        print(f"* flags                : {item_flags}")
    if item_trace is not None:
        print(f"* trace")
        parse_trace(item_trace)


def main():
    parser = argparse.ArgumentParser(description='shadowhook record parser.')
    parser.add_argument('-m', '--more-info', action='store_true', help='show more info')
    parser.add_argument('-a', '--maps', default='', required=False, help='maps file (linux /proc/pid/maps format)')
    parser.add_argument('-f', '--item-flags', default='111111111111', required=False, help='record item flags')
    parser.add_argument('-i', '--input-file', default='./input.txt', required=False, help='record file')
    parser.add_argument('-l', '--input-line', default='', required=False, help='record line')
    args = parser.parse_args()

    global g_more_info
    if args.more_info: g_more_info = True

    if args.maps:
        parse_maps(args.maps)

    if len(args.item_flags) != ITEM_COUNT:
        raise ValueError(f"The item-flags must be {ITEM_COUNT} characters long")
    if set(args.item_flags) > {'0', '1'}:
        raise ValueError(f"The item-flags can only contain 0 and 1.")
    if args.item_flags[ITEM_OP_IDX] != '1':
        raise ValueError(f"The OP flag in item-flags must be 1.")

    if len(args.input_line) > 0:
        parse_line(args.input_line, args.item_flags)
    else:
        with open(args.input_file, 'r') as f:
            i = 1
            for line in f:
                idx = line.find(LOGCAT_TAG)
                if idx > 0:
                    line = line[idx + len(LOGCAT_TAG):]
                line = line.strip()
                if len(line) > 0:
                    print(f"\n# record {i}\n----------------------------------------------------------------------")
                    i += 1
                    parse_line(line, args.item_flags)
    
    print('\n** END **')


if __name__ == '__main__':
    sys.exit(main())
