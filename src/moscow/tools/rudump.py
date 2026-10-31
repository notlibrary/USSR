#!/usr/bin/env python3
"""Disassemble a .ru file (docs/RU_BYTECODE_FORMAT.md, format 3).

    python rudump.py program.ru            listing
    python rudump.py --summary program.ru  opcodes and OOP command names used

Moscow's runtime (arch/amd64/ussr_rt.nasm) implements a subset of the VM;
the summary shows what a program needs from it.
"""
import struct
import sys

OPS = {
    0x00: "NOP", 0x01: "LOAD_CONST", 0x02: "LOAD_VAR", 0x03: "STORE_VAR",
    0x04: "LOAD_HASH", 0x05: "STORE_HASH", 0x06: "DECODE_UNO", 0x07: "GET",
    0x08: "VECTOR_GET", 0x09: "VECTOR_SET", 0x10: "ADD", 0x11: "SUB",
    0x12: "MUL", 0x13: "DIV", 0x14: "MOD", 0x15: "NEG", 0x16: "EQ",
    0x17: "NE", 0x18: "LT", 0x19: "LE", 0x1a: "GT", 0x1b: "GE",
    0x1c: "LAND", 0x1d: "LOR", 0x1e: "XOR", 0x1f: "BAND", 0x20: "BOR",
    0x21: "SHL", 0x22: "SHR", 0x30: "JMP", 0x31: "JMP_TRUE",
    0x32: "JMP_FALSE", 0x40: "SET", 0x41: "PRINT", 0x42: "CONCAT",
    0x43: "EXTERNAL", 0x44: "OOP", 0x45: "EVAL", 0x46: "RANDOM64",
    0x47: "SEED64", 0x48: "SCAN", 0x49: "TIME", 0x4a: "CHAIN",
    0x4b: "FILE", 0x4c: "CD", 0x4d: "TEMPLATE", 0x50: "CALL", 0x51: "RET",
    0x52: "RETURN", 0xf0: "HALT",
}
# opcodes whose immediate is a string-pool index
STRING_IMM = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x40, 0x43, 0x44, 0x45,
              0x48, 0x4d}


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.d, self.p)[0]
        self.p += 8
        return v

    def string(self):
        n = self.u32()
        if n == 0xFFFFFFFF:
            return None
        s = self.d[self.p:self.p + n].decode("utf-8", "replace")
        self.p += n
        return s

    def value(self):
        tag = self.u8()
        if tag == 0:
            return None
        if tag == 1:
            return self.i64()
        if tag == 2:
            return struct.unpack("<d", struct.pack("<q", self.i64()))[0]
        if tag == 3:
            return self.string()
        if tag == 4:
            return bool(self.u8())
        raise ValueError("constant tag %d" % tag)

    def expression(self):
        tag = self.u8()
        if tag == 0:
            self.value()
        elif tag == 1:
            self.string()
        elif tag == 2:
            self.u8()
            self.expression()
            self.expression()
        elif tag == 3:
            self.u8()
            self.expression()

    def block(self):
        for _ in range(self.u32()):
            self.string()
            self.string()
            self.string()
            for _ in range(self.u8()):
                kind = self.u8()
                self.u8()
                if kind == 0:
                    self.value()
                elif kind in (1, 2):
                    self.expression()
                elif kind == 3:
                    self.string()
                else:
                    self.block()

    def sites(self):
        out = []
        for _ in range(self.u32()):
            ins = self.u32()
            name = self.string()
            ret = self.string()
            args = []
            for _ in range(self.u8()):
                kind = self.u8()
                assign = self.u8()
                if kind == 1:
                    args.append(("var", self.string(), assign))
                elif kind in (2, 3):
                    self.block()
                    args.append(("block", None, assign))
                else:
                    args.append(("value", None, assign))
            out.append((ins, name, ret, args))
        return out


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89USSR\r\n\x1a":
        raise SystemExit("%s: not a .ru file" % path)
    r = Reader(data)
    r.p = 24
    code = []
    for _ in range(r.u32()):
        op, a, b, c, imm = struct.unpack_from("<BBBBI", data, r.p)
        r.p += 8
        code.append((op, a, b, c, imm))
    consts = [r.value() for _ in range(r.u32())]
    strings = [r.string() for _ in range(r.u32())]
    funcs = []
    for _ in range(r.u32()):
        name = r.string()
        ret = r.string()
        entry = r.u32()
        params = [r.string() for _ in range(r.u32())]
        locs = [r.string() for _ in range(r.u32())]
        funcs.append((name, ret, entry, params, locs))
    oop_sites = r.sites()
    scan_sites = r.sites()
    return code, consts, strings, funcs, oop_sites, scan_sites


def main():
    args = sys.argv[1:]
    summary = args and args[0] == "--summary"
    if summary:
        args = args[1:]
    code, consts, strings, funcs, oop_sites, scan_sites = load(args[0])
    if summary:
        used = {}
        names = {}
        for op, a, b, c, imm in code:
            used[OPS.get(op, hex(op))] = used.get(OPS.get(op, hex(op)), 0) + 1
            if op in (0x44, 0x43):
                key = (OPS[op], strings[imm])
                names[key] = names.get(key, 0) + 1
        print("opcodes:", " ".join("%s:%d" % kv for kv in sorted(used.items())))
        print("commands:", " ".join("%s/%s:%d" % (k[0], k[1], v) for k, v in sorted(names.items())))
        print("sizes: code %d consts %d strings %d functions %d oop sites %d scan sites %d"
              % (len(code), len(consts), len(strings), len(funcs), len(oop_sites), len(scan_sites)))
        return
    entries = {f[2]: f for f in funcs}
    for i, (op, a, b, c, imm) in enumerate(code):
        if i in entries:
            f = entries[i]
            print("%s(%s): %s   locals %s" % (f[0], f[1], " ".join(f[3]), f[4]))
        extra = ""
        if op == 0x01:
            extra = repr(consts[imm])
        elif op in STRING_IMM:
            extra = strings[imm]
        elif op == 0x50:
            extra = funcs[imm][0]
        print("%5d  %-10s a=%-2d b=%-2d c=%-2d imm=%-5d %s" % (i, OPS.get(op, hex(op)), a, b, c, imm, extra))
    for s in oop_sites:
        print("oop site", s)


if __name__ == "__main__":
    main()
