package com.mp2r.android;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Checks small executable/mod files before starting the native process. No archives are rewritten. */
final class GameDataCheck {
    static final String EXE_SHA256 = "1e25cafc37174a607b58a38dfac796056f0f38f35d253b0b2d7677f9bd7bd010";
    static final String WIDESCREEN_SHA256 = "3ba1f25bd374f104aa4833b84823113a111456e8b4d49969b300202cee6af111";
    enum Kind { MISSING, UNREADABLE, EXECUTABLE_VERSION, WIDESCREEN_MISSING, WIDESCREEN_VERSION, WIDESCREEN_INI }
    static final class Failure {
        final Kind kind; final String path;
        Failure(Kind kind, String path) { this.kind = kind; this.path = path; }
        boolean canDisableWidescreen() { return kind == Kind.WIDESCREEN_MISSING || kind == Kind.WIDESCREEN_VERSION || kind == Kind.WIDESCREEN_INI; }
        @Override public String toString() { return kind + ": " + path; }
    }
    static Failure inspect(File root, boolean widescreen) {
        for (String name : new String[] {"MaxPayne2.exe", "MP2_Data.ras", "MP2_Init.ras", "e2mfc.dll", "e2driver/e2_d3d8_driver_mfc.dll"}) {
            File file = new File(root, name);
            if (!file.isFile() || file.length() == 0) return new Failure(Kind.MISSING, name);
            if (!file.canRead()) return new Failure(Kind.UNREADABLE, name);
        }
        try {
            if (!matches(new File(root, "MaxPayne2.exe"), EXE_SHA256)) return new Failure(Kind.EXECUTABLE_VERSION, "MaxPayne2.exe");
        } catch (IOException e) { return new Failure(Kind.UNREADABLE, "MaxPayne2.exe"); }
        return widescreen ? inspectWidescreen(root) : null;
    }
    static Failure inspectWidescreen(File root) {
        File mod = new File(root, "scripts/MaxPayne2.WidescreenFix.asi");
        if (!mod.isFile()) return new Failure(Kind.WIDESCREEN_MISSING, "scripts/MaxPayne2.WidescreenFix.asi");
        try {
            if (!matches(mod, WIDESCREEN_SHA256)) return new Failure(Kind.WIDESCREEN_VERSION, "scripts/MaxPayne2.WidescreenFix.asi");
        } catch (IOException e) { return new Failure(Kind.UNREADABLE, "scripts/MaxPayne2.WidescreenFix.asi"); }
        File ini = new File(root, "scripts/MaxPayne2.WidescreenFix.ini");
        return ini.isFile() && ini.canRead() ? null : new Failure(Kind.WIDESCREEN_INI, "scripts/MaxPayne2.WidescreenFix.ini");
    }
    static boolean matches(File file, String expected) throws IOException {
        try (FileInputStream in = new FileInputStream(file)) {
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[16384];
            for (int n; (n = in.read(buffer)) != -1;) hash.update(buffer, 0, n);
            StringBuilder result = new StringBuilder();
            for (byte b : hash.digest()) result.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            return result.toString().equalsIgnoreCase(expected);
        } catch (NoSuchAlgorithmException e) { throw new AssertionError(e); }
    }
}
