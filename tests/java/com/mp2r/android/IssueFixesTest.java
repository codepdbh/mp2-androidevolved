package com.mp2r.android;

import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

public final class IssueFixesTest {
    static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.GERMANY);
        String ini = "[OTHER]\r\nFOVFactor=9\r\n[MAIN]\r\n\r\n FOVFactor = 1.0 // keep\r\nFixHud=0\r\n[END]\r\nWidescreenHud=9\r\n";
        String updated = WidescreenConfig.update(ini, 1.25f, true, 70, 2, false);
        check(updated.contains(" FOVFactor = 1.25 // keep"), "Locale and comment preservation");
        check(updated.contains("[OTHER]\r\nFOVFactor=9") && updated.contains("[END]\r\nWidescreenHud=9"), "Other sections untouched");
        check(updated.contains("FixHud=1") && updated.contains("WidescreenHudOffset = 70"), "Edge HUD correction");
        check(updated.equals(WidescreenConfig.update(updated, 1.25f, true, 70, 2, false)), "INI update idempotence");
        String minimal = WidescreenConfig.update("[MAIN]", 1, false, 120, 0, true);
        check(minimal.startsWith("[MAIN]\n") && minimal.contains("WidescreenHudOffset = 100"), "EOF section and offset clamp");
        check(WidescreenConfig.update("[MAIN]\nFOVFactor=\n", 1, false, 0, 0, true).contains("FOVFactor=1.00\n"), "Empty value");
        check(WidescreenConfig.update(";owner comment", 1, false, 0, 0, true).contains("[MAIN]\n"), "Missing MAIN");
        File root = Files.createTempDirectory("mp2-preflight").toFile();
        try {
            check(GameDataCheck.inspect(root, false).kind == GameDataCheck.Kind.MISSING, "Missing core data");
            check(GameDataCheck.inspectWidescreen(root).kind == GameDataCheck.Kind.WIDESCREEN_MISSING, "Missing ASI");
            File scripts = new File(root, "scripts"); scripts.mkdirs();
            File asi = new File(scripts, "MaxPayne2.WidescreenFix.asi");
            Files.write(asi.toPath(), "abc".getBytes(StandardCharsets.UTF_8));
            check(GameDataCheck.matches(asi, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 known vector");
            check(!GameDataCheck.matches(asi, GameDataCheck.WIDESCREEN_SHA256), "Reject incompatible ASI");
            check(GameDataCheck.inspectWidescreen(root).kind == GameDataCheck.Kind.WIDESCREEN_VERSION, "Report version mismatch");
        } finally {
            try (java.util.stream.Stream<java.nio.file.Path> paths = Files.walk(root.toPath())) {
                paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> { try { Files.delete(path); } catch (Exception e) { throw new RuntimeException(e); } });
            }
        }
        StringBuilder noisy = new StringBuilder("01-01 00:00 12 12 E NFSC: Guest integration boundary: missing ASI\n");
        for (int i=0; i<2000; i++) noisy.append("01-01 00:00 12 12 W OEM: unrelated ").append(i).append('\n');
        check(ReportExcerpt.select(noisy.toString(), 60).contains("missing ASI"), "Root failure survives OEM noise");
        for (int i=0; i<2000; i++) noisy.append("01-01 00:00 12 12 W NFSC: repeated warning\n");
        check(ReportExcerpt.select(noisy.toString(), 60).contains("missing ASI"), "Root failure survives repeated port warnings");
        check(ReportExcerpt.select("Fatal signal 7 (SIGBUS)\nOEM message", 60).contains("SIGBUS"), "Native signal retained");
        check(ReportExcerpt.select("", 60).isEmpty(), "Empty log");
        System.out.println("Issue fixes: all Java regression checks passed");
    }
}
