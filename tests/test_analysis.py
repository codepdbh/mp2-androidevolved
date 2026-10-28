import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/f'{name}.py')
    result=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

class AnalysisTests(unittest.TestCase):
    def test_vu0_comparison_translation_is_reproducible(self):
        fix=module('fix_vu0_comparisons')
        source='''    // 0x4ae68c: 0x4be1102c  vsub.xyzw   $vf0, $vf2, $vf1
    ctx->pc = 0x4ae68cu;
    { __m128 res = PS2_VSUB(ctx->vu0_vf[2], ctx->vu0_vf[1]); __m128i mask = _mm_set_epi32(-1, -1, -1, -1); ctx->vu0_vf[0] = PS2_VBLEND(ctx->vu0_vf[0], res, _mm_castsi128_ps(mask)); }
'''
        fixed,count=fix.fix_text(source)
        self.assertEqual(count,1)
        self.assertIn('vu0CompareSub(ctx,ctx->vu0_vf[2],ctx->vu0_vf[1],15)',fixed)
        self.assertNotIn('ctx->vu0_vf[0] =',fixed)
        self.assertEqual(fix.fix_text(fixed),(fixed,0))
        with self.assertRaises(ValueError): fix.fix_text(source.replace('0x4be1102c','0x4be1082c'))

    def test_original_elf_identity(self):
        info=module('analyze_iso').elf_info(ROOT/'private/disc/extracted/SLES_522.56')
        self.assertEqual(info['machine'],8)
        self.assertEqual(info['entry'],'0x100008')
        self.assertEqual(info['sha256'],'9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba')
        s=info['segments'][0]
        self.assertLess(int(info['entry'],16),s['vaddr']+s['filesz'])

    def test_forced_fullscreen_is_rejected(self):
        dev=module('development')
        self.assertTrue(dev.policy()['desktop']['windowed_only'])
        source=(ROOT/'config/development.toml').read_text()
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'bad.toml'
            p.write_text(source.replace('fullscreen_allowed = false','fullscreen_allowed = true'))
            with self.assertRaises(ValueError): dev.policy(p)

if __name__=='__main__': unittest.main()
