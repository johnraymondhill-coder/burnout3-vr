"""Run upstream regen.sh's NTSC-U stages with Windows-native Python.

All generated files stay in upstream's ignored local build directories.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
toolkit = root.parent / 'xboxrecomp'
xbe = root / 'Burnout 3 Takedown/default.xbe'
out = root / 'build/xr'
out.mkdir(parents=True, exist_ok=True)
cfg = root / 'config'
def run(name, args):
    log = out / (name + '.log')
    print(name, flush=True)
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run([sys.executable, '-m', 'tools.' + name, str(xbe), *map(str, args)],
                                cwd=toolkit, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding='utf-8', errors='replace')[-6000:])
        raise SystemExit(result.returncode)

if not (out / 'disasm/functions.json').exists():
    run('xbe_parser', ['--json', out/'analysis.json', '--quiet'])
    run('disasm', ['--analysis-json',out/'analysis.json','--seed-functions',cfg/'seed_functions.json',
                   '-o',out/'disasm','-v'])
run('func_id', ['--functions',out/'disasm/functions.json','--strings',out/'disasm/strings.json',
                '--xrefs',out/'disasm/xrefs.json','-o',out/'func_id'])
run('abi_analysis', ['--functions',out/'disasm/functions.json','--identified',out/'func_id/identified_functions.json',
                     '--output-dir',out/'abi'])
run('recomp', ['--all','--split','1000','--disasm-dir',out/'disasm','--func-id-dir',out/'func_id',
               '--abi-dir',out/'abi','--exclude-manual',root/'src/game/recomp/recomp_manual.c',
               '--spin-waits',cfg/'spin_waits.json','--patches',cfg/'patches.json',
               '--mem-order',cfg/'mem_order.json','--gen-dir',root/'src/game/recomp/gen',
               '-o',out/'recomp'])
print('Source generation complete.', flush=True)
