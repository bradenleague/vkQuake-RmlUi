#!/usr/bin/env python3
"""Run a pipeline of commands separated by '&&' tokens.

Meson's custom_target command lists don't support '&&' as a shell
operator on Windows — it gets passed as a literal argument. This
script splits the argument list on '&&' and runs each command in
sequence, aborting on the first failure.
"""
import subprocess
import sys

commands = []
current = []
for arg in sys.argv[1:]:
    if arg == '&&':
        if current:
            commands.append(current)
            current = []
    else:
        current.append(arg)
if current:
    commands.append(current)

for cmd in commands:
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(result.returncode)
