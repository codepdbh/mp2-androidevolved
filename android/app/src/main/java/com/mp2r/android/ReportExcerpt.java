package com.mp2r.android;

import java.util.ArrayDeque;

/** Keep port failures ahead of unrelated OEM warnings in GitHub's limited URL body. */
final class ReportExcerpt {
    static String select(String text, int lines) {
        ArrayDeque<String> critical = new ArrayDeque<>(), warnings = new ArrayDeque<>(), context = new ArrayDeque<>(), fallback = new ArrayDeque<>();
        for (String line : text.split("\n")) {
            keep(fallback, line, lines);
            boolean own = line.contains("NFSC") || line.contains(" MP2") || line.contains("MP2Report") || line.contains("MP2 failure");
            boolean failure = line.contains("Guest integration boundary") || line.contains("Fatal signal")
                || line.contains("FATAL EXCEPTION") || line.contains(" F DEBUG") || line.contains("*** *** ***")
                || line.contains("WIDESCREEN_") || line.contains("EXECUTABLE_VERSION") || line.contains("MP2 failure");
            if (failure) keep(critical, line, lines);
            else if (own && (line.contains(" E ") || line.contains(" W "))) keep(warnings, line, lines);
            else if (own) keep(context, line, lines);
        }
        if (critical.isEmpty() && warnings.isEmpty() && context.isEmpty()) return String.join("\n", fallback);
        StringBuilder result = new StringBuilder(String.join("\n", critical));
        int room = lines - critical.size();
        while (warnings.size() > room) warnings.removeFirst();
        if (!warnings.isEmpty()) result.append("\n-- Port warnings --\n").append(String.join("\n", warnings));
        room -= warnings.size();
        while (context.size() > room) context.removeFirst();
        if (!context.isEmpty()) result.append("\n-- Port context --\n").append(String.join("\n", context));
        return result.toString();
    }
    private static void keep(ArrayDeque<String> queue, String line, int limit) {
        queue.addLast(line);
        if (queue.size() > limit) queue.removeFirst();
    }
}
