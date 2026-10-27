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
    if subprocess.check_output(['git','-C',str(path),'status','--porcelain','--ignore-submodules=all'],text=True).strip():
        raise SystemExit(f'Dirty dependency: {path}; refusing automatic changes')
    print(row['path'],actual)
