"""Run the original upstream exporter headlessly with askFile script arguments."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import shutil
from development import ROOT,run

p=argparse.ArgumentParser()
p.add_argument('--process',action='store_true',help='Export an existing project without repeating analysis')
p.add_argument('--script',help='Run one project-owned evidence script instead of re-exporting all functions')
p.add_argument('--kernel',action='store_true',help='Analyze the private byte-identical relocated alarm patch ELF')
p.add_argument('--tlb-kernel',action='store_true',help='Analyze the private byte-identical relocated TLB service ELF')
a=p.parse_args()
if a.kernel and a.tlb_kernel: p.error('Select one copied kernel block')
kernel_kind='alarm' if a.kernel else 'tlb' if a.tlb_kernel else None
elf=ROOT/'private/disc/extracted/SLES_522.56'
with elf.open('rb') as f:
    if hashlib.file_digest(f,'sha256').hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
        raise SystemExit('Wrong ELF identity')
if kernel_kind:
    import json
    elf=ROOT/f'private/{kernel_kind}-kernel.elf'
    expected=json.loads((ROOT/f'private/{kernel_kind}-kernel-plan.json').read_text())['elf_sha256']
    with elf.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=expected: raise SystemExit('Wrong copied-code ELF identity')
home=ROOT/'external/ghidra_12.1.4_PUBLIC'
headless=home/'support/analyzeHeadless.bat'
if not headless.exists(): raise SystemExit('Run scripts/setup_ghidra.py first')
out=ROOT/('private/ghidra-kernel' if a.kernel else 'private/ghidra-tlb-kernel' if a.tlb_kernel else 'private/ghidra')
out.mkdir(parents=True,exist_ok=True)
os.environ['JAVA_HOME']=r'C:\Program Files\Java\jdk-25.0.2'
os.environ['GHIDRA_HEADLESS_MAXMEM']='1200M'
os.environ['GHIDRA_HEADLESS_JAVA_OPTIONS']='-Djava.awt.headless=true -XX:ActiveProcessorCount=2'
scripts=out/'scripts'
scripts.mkdir(exist_ok=True)
for source in [*(ROOT/'scripts/ghidra').glob('*.java'),
               ROOT/'external/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java']:
    shutil.copy2(source,scripts/source.name)
scriptpaths=str(scripts)
command=['cmd.exe','/d','/c',str(headless),str(out),'MP2R-Kernel' if a.kernel else 'MP2R-TLB' if a.tlb_kernel else 'MP2R',
    '-scriptPath',scriptpaths,'-max-cpu','2','-log',str(out/'analysis.log'),
    '-scriptlog',str(out/'scripts.log')]
if a.process:
    command+=['-process',elf.name,'-noanalysis']
else:
    command+=['-import',str(elf),'-processor','r5900:LE:32:default',
              '-analysisTimeoutPerFile','240','-preScript','MP2AnalysisOptions.java',str(out/'options.txt')]
if a.script:
    if Path(a.script).name!=a.script or not (ROOT/'scripts/ghidra'/a.script).is_file(): raise SystemExit('Unknown local script')
    command+=['-postScript',a.script,str(out)]
else:
    command+=['-postScript','ExportPS2Functions.java',str(out/'mp2-pal.toml'),str(out/'mp2-pal.csv')]
    command+=['-postScript','MP2ExportEvidence.java',str(out)]
log_path=ROOT/('private/logs/ghidra-process.log' if a.process else 'private/logs/ghidra-headless.log')
status=run(command,log_path)
if status==0 and 'REPORT SCRIPT ERROR' in log_path.read_text(encoding='utf-8',errors='replace'):
    raise SystemExit('Ghidra reported a script failure despite exit status zero; inspect '+str(log_path))
raise SystemExit(status)
