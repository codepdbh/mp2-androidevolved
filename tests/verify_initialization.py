import hashlib
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
elf=root/'private/disc/extracted/SLES_522.56'
with elf.open('rb') as f:
    if hashlib.file_digest(f,'sha256').hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
        raise SystemExit('PAL identity mismatch')
raise SystemExit(subprocess.run([sys.argv[1],str(elf)],timeout=30).returncode)
