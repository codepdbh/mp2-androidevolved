"""Run the Android-independent Java regression suite with a local JDK."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidIssueHelpers(unittest.TestCase):
    def test_java_helpers(self):
        java_home = os.environ.get("JAVA_HOME")
        candidates = [Path(java_home) / "bin" if java_home else None,
                      Path("C:/Program Files/Android/Android Studio/jbr/bin")]
        javac = shutil.which("javac")
        if not javac:
            javac = next((str(p / "javac.exe") for p in candidates if p and (p / "javac.exe").is_file()), None)
        if not javac:
            self.skipTest("A JDK is required for the Android helper regression suite")
        java = Path(javac).with_name("java.exe" if os.name == "nt" else "java")
        sources = ROOT / "android/app/src/main/java/com/mp2r/android"
        with tempfile.TemporaryDirectory(prefix="mp2-java-tests-") as classes:
            subprocess.run([javac, "-d", classes,
                            *(str(sources / name) for name in ("GameDataCheck.java", "WidescreenConfig.java", "ReportExcerpt.java")),
                            str(ROOT / "tests/java/com/mp2r/android/IssueFixesTest.java")], check=True)
            subprocess.run([str(java), "-cp", classes, "com.mp2r.android.IssueFixesTest"], check=True)
