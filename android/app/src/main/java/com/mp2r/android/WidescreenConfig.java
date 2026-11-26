package com.mp2r.android;

import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Updates only MAIN options, preserving the rest of the owner's INI. */
final class WidescreenConfig {
    static String update(String text, float fov, boolean hud, int offset, int borders, boolean comics) {
        text = set(text, "FOVFactor", String.format(Locale.ROOT, "%.2f", fov));
        if (hud) text = set(text, "FixHud", "1");
        text = set(text, "WidescreenHud", hud ? "1" : "0");
        text = set(text, "WidescreenHudOffset", Integer.toString(Math.max(0, Math.min(100, offset))));
        text = set(text, "CutsceneBorders", Integer.toString(borders));
        return set(text, "ComicsMode", comics ? "1" : "0");
    }
    private static String set(String text, String key, String value) {
        Pattern section = Pattern.compile("(?im)^\\[MAIN\\][^\\r\\n]*(?:\\r?\\n|$)");
        Matcher main = section.matcher(text);
        if (!main.find()) return text + (text.endsWith("\n") ? "" : "\n") + "[MAIN]\n" + key + " = " + value + "\n";
        int start = main.end();
        Matcher next = Pattern.compile("(?m)^\\[").matcher(text); int end = next.find(start) ? next.start() : text.length();
        String body = text.substring(start, end);
        Matcher setting = Pattern.compile("(?im)^([\\t ]*" + Pattern.quote(key) + "[\\t ]*=[\\t ]*)[^\\s/;\\r\\n]*").matcher(body);
        if (setting.find()) body = setting.replaceFirst(Matcher.quoteReplacement(setting.group(1) + value));
        else body = key + " = " + value + "\n" + body;
        String prefix = text.substring(0, start);
        return prefix + (prefix.endsWith("\n") ? "" : "\n") + body + text.substring(end);
    }
}
