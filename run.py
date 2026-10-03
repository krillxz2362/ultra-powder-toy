#!/usr/bin/env python3
"""Запуск Lua-файла на LuaJIT 2.1 (через lupa), чтобы проверять физику без телефона."""
import sys
from lupa.luajit21 import LuaRuntime

path = sys.argv[1] if len(sys.argv) > 1 else "test.lua"
L = LuaRuntime(unpack_returned_tuples=True)
L.execute('package.path = "/home/user/sandbox/game/?.lua;" .. package.path')
with open(path, encoding="utf-8") as f:
    src = f.read()
chunk = L.eval("function(src, name) return assert(loadstring(src, name)) end")(src, "@" + path)
chunk()
