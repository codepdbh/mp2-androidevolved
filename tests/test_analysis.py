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
