"""Restore exact audited sources without recursive test submodules."""
import argparse
import json
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--restore',action='store_true',help='Fetch missing checkouts; otherwise verify only')
a=p.parse_args()
for row in json.loads((root/'config/dependencies.lock.json').read_text()):
    path=root/row['path']
    if not (path/'.git').exists():
        if not a.restore: raise SystemExit(f'Missing dependency: {path}')
        path.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','init',str(path)],check=True)
        subprocess.run(['git','-C',str(path),'remote','add','origin',row['url']],check=True)
        subprocess.run(['git','-C',str(path),'fetch','--depth','1','origin',row['commit']],check=True,timeout=180)
        subprocess.run(['git','-C',str(path),'checkout','--detach',row['commit']],check=True)
    actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    if actual!=row['commit']: raise SystemExit(f'Commit mismatch: {path}; refusing to change existing checkout')
    # Test/documentation submodules are neither restored nor used in the build.
    status=lambda: subprocess.check_output(['git','-C',str(path),'status','--porcelain','--ignore-submodules=all'],text=True).strip()
    patches=[str(root/patch) for patch in row.get('patches',[])]
    def git_apply(*flags):
        return subprocess.run(['git','-C',str(path),'apply','--whitespace=nowarn',*flags,*patches],capture_output=True).returncode==0
    if not status():
        if patches and not (a.restore and git_apply()): raise SystemExit(f'Unpatched dependency: {path}; run with --restore')
    elif not patches or not git_apply('--reverse','--check'):
        raise SystemExit(f'Dirty dependency: {path}; refusing automatic changes')
    touched={line.split()[-1] for line in status().splitlines()}
    expected={line[6:].strip() for patch in patches for line in Path(patch).read_text().splitlines() if line.startswith('+++ b/')}
    if touched!=expected: raise SystemExit(f'Unexpected changes in {path}: {sorted(touched-expected)}')
    print(row['path'],actual)
