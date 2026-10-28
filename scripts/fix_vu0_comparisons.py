"""Repair the PAL VSUB-to-VF0/CFC2 comparison idiom in generated code.

Only recognized vector-vector VSUB encodings with destination VF0 are changed.
Other translations remain untouched. This pass is idempotent and also runs on
the recompiler staging output before files are copied into the active build.
"""
import argparse
from pathlib import Path
import re

PATTERN=re.compile(r'(?P<head>// 0x[0-9a-f]+: 0x(?P<word>[0-9a-f]+)  vsub\.(?:xyzw|xyz)\s+\$vf0[^\n]*\n\s+ctx->pc = [^\n]+\n)'
                   r'\s*\{ __m128 res = PS2_VSUB\(ctx->vu0_vf\[(?P<fs>\d+)\], ctx->vu0_vf\[(?P<ft>\d+)\]\);[^\n]*ctx->vu0_vf\[0\] = [^\n]+; \}')

def fix_text(text):
    def replace(m):
        word=int(m['word'],16)
        if word>>26!=0x12 or word&63!=0x2c or (word>>6)&31:
            raise ValueError('Unexpected VSUB encoding')
        fs,ft=(word>>11)&31,(word>>16)&31
        if fs!=int(m['fs']) or ft!=int(m['ft']): raise ValueError('VSUB operands disagree with instruction')
        return m['head']+f'    mp2r::vu0CompareSub(ctx,ctx->vu0_vf[{fs}],ctx->vu0_vf[{ft}],{(word>>21)&15});'
    result,count=PATTERN.subn(replace,text)
    if count and '#include "vu0_compare.h"' not in result: result='#include "vu0_compare.h"\n'+result
    return result,count

def fix_tree(directory):
    instructions=files=0
    for path in sorted(directory.glob('mp2_*.cpp')):
        before=path.read_text(); after,count=fix_text(before)
        if count: path.write_text(after); instructions+=count; files+=1
    print(f'VU0 comparisons: repaired {instructions} instructions in {files} files')
    return instructions,files

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--directory',type=Path,default=Path(__file__).resolve().parents[1]/'private/generated/full')
    fix_tree(p.parse_args().directory)
