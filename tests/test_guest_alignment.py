import importlib.util
import json
from pathlib import Path
import re
import unittest

spec = importlib.util.spec_from_file_location("alignment", Path(__file__).parents[1] / "scripts/pc_recomp/fix_guest_alignment.py")
alignment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(alignment)


class AlignmentTests(unittest.TestCase):
    def test_reported_fstp_and_fld(self):
        source = "*(double*)ADDR(esi + 0x5F3) = fp_pop(); fp_push(*(double*)ADDR(esi + 0x5F3));"
        result = alignment.fix_text(source)
        self.assertEqual(result.count("*(recomp_f64_una*)ADDR(esi + 0x5F3)"), 2)
        self.assertNotIn("*(double*)ADDR", result)
        self.assertEqual(alignment.fix_text(result), result)

    def test_float_and_non_guest_pointers(self):
        source = "*(float*)ADDR(eax) = 1; float f = *(float*)&bits; double *host;"
        result = alignment.fix_text(source)
        self.assertIn("*(recomp_f32_una*)ADDR(eax)", result)
        self.assertIn("*(float*)&bits", result)
        self.assertIn("double *host", result)

    def test_preflight_hashes_match_recompiled_modules(self):
        root = Path(__file__).parents[1]
        modules = json.loads((root / "config/pc/modules.json").read_text(encoding="utf-8"))["modules"]
        java = (root / "android/app/src/main/java/com/mp2r/android/GameDataCheck.java").read_text(encoding="utf-8")
        for constant, filename in (("EXE_SHA256", "MaxPayne2.exe"), ("WIDESCREEN_SHA256", "MaxPayne2.WidescreenFix.asi")):
            expected = next(module["sha256"] for module in modules if Path(module["file"]).name.lower() == filename.lower())
            self.assertEqual(re.search(constant + r' = "([a-f0-9]+)"', java).group(1), expected)
