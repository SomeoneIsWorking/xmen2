package com.someoneisworking.xmen2;

import android.app.Activity;

import java.io.File;
import java.io.IOException;

/** Debug-only app-private source for exercising the native runner on a device. */
final class XMen2DebugInstall {
    static final String EXTRA_PRIVATE_INSTALL =
            "com.someoneisworking.xmen2.debug.private_install";
    private static final String DIRECTORY_NAME = "debug-game";

    private XMen2DebugInstall() {}

    static File requestedSource(Activity activity) {
        if (!BuildConfig.DEBUG || !activity.getIntent().getBooleanExtra(EXTRA_PRIVATE_INSTALL, false)) {
            return null;
        }
        try {
            File canonicalRoot = activity.getFilesDir().getCanonicalFile();
            File source = new File(canonicalRoot, DIRECTORY_NAME).getCanonicalFile();
            return source.getParentFile().equals(canonicalRoot) && source.isDirectory() ? source : null;
        } catch (IOException error) {
            return null;
        }
    }
}
