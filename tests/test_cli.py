"""Exercise the executable's user-visible behavior, not just core functions."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else './cpu16_sim').resolve())

def run(*args, ok=True, input=None):
    proc = subprocess.run([exe, *map(str, args)], input=input, text=True,
                          capture_output=True, timeout=20)
    assert (proc.returncode == 0) == ok, (args, proc.stdout, proc.stderr)
    return proc

with tempfile.TemporaryDirectory(prefix='cli-', dir='build') as directory:
    root = Path(directory)
    asm, report, table, coe = [root / name for name in ('add.asm', 'out.json', 'out.csv', 'out.coe')]
    asm.write_text('LOAD 0x80\nADD 0x81\nHALT\n.word 0x80 5\n.word 0x81 3\n')
    for mode in ('lockstep', 'micro', 'isa'):
        proc = run('-a', asm, '--mode', mode, '--json', report, '--csv', table)
        assert len(proc.stdout.splitlines()) == 3
        data = json.loads(report.read_text())
        assert data['halted'] and data['registers']['ACC_signed'] == 8
        assert data['instructions_executed'] == 3 and data['micro_cycles'] == 22
        assert data['lockstep_verified'] == (mode == 'lockstep')
        with table.open() as fp:
            row = next(csv.DictReader(fp))
        assert row['micro_cycles'] == '22' and row['ACC_signed'] == '8'
    proc = run('-a', asm, '--trace', '--json', report)
    assert json.loads(report.read_text())['lockstep_verified']
    assert len(proc.stdout.splitlines()) == 6
    assert 'PC=' in run('-a', asm, '--verbose').stdout
    run('-a', asm, '-o', coe)
    run('-m', coe)
    assert 'Halted.' in run('-a', asm, '--debug', input='s\nm\nc\nr\nx 0 3\nq\n').stdout
    assert 'Invalid address' in run('-a', asm, '--debug', input='b bad\nq\n').stdout
    for args in [(), ('-a',), ('-a', asm, '-m', coe),
                 ('-a', asm, '--mode', 'typo'), ('--unknown',), ('-a', root / 'absent')]:
        run(*args, ok=False)
    run('--help')
    for mode in ('lockstep', 'micro', 'isa'):
        asm.write_text('JMP 0\n')
        assert 'limit' in run('-a', asm, '--mode', mode, ok=False).stderr.lower()
        asm.write_text('.word 0 0xff00\n')
        run('-a', asm, '--mode', mode, ok=False)

    # An altered LOAD microinstruction must still be caught with --trace.
    rom = root / 'bad.coe'
    words = ['401', '9', '11', '2', '2', '2', '2', '2', '61', '9', '4']
    rom.write_text('memory_initialization_radix=16;\nmemory_initialization_vector=' + ','.join(words) + ';\n')
    asm.write_text('LOAD 0x80\nHALT\n.word 0x80 5\n')
    assert '[FAIL]' in run('-a', asm, '-c', rom, '--trace', ok=False).stderr

print('PASS: CLI modes, concise output, trace verification, debugger, reports and errors')
