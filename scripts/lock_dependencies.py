import json
from pathlib import Path
import subprocess

paths=['external/PS2Recomp','external/revc-reference','external/librw',
       'external/ghidra-emotionengine-reloaded','external/json','external/sse2neon']
paths += ['build/ps2recomp/_deps/'+name+'-src' for name in ('elfio','toml11','fmt','libdwarf','rabbitizer')]
def git(p,*args): return subprocess.check_output(['git','-C',p,*args],text=True).strip()
rows=[dict(path=p,url=git(p,'remote','get-url','origin'),commit=git(p,'rev-parse','HEAD')) for p in paths]
Path('config/dependencies.lock.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
