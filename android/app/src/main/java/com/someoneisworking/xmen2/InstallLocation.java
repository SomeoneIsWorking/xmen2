package com.someoneisworking.xmen2;

import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;

import java.io.File;

/**
 * Resolves a Storage Access Framework selection to the filesystem path the
 * native install validator reads.
 *
 * The port reads the install in place. Copying it into app-private storage
 * costs a content-provider round trip per file, which measured at 0.05 MB/s
 * across the game's many small files -- twelve minutes for a 2.2 GB install,
 * and a duplicate copy of it on the device. With all-files access the same
 * bytes are already readable where they sit.
 *
 * A document URI is not required to have a filesystem path, so resolution
 * fails rather than guessing: the caller reports which selection cannot be
 * used instead of silently staging a slow copy.
 */
final class InstallLocation {
    private InstallLocation() {
    }

    /** The path a picked folder refers to, or null if it has no filesystem path. */
    static File fromTree(Uri tree) {
        return fromDocumentId(DocumentsContract.getTreeDocumentId(tree));
    }

    /** The path a picked document refers to, or null if it has no filesystem path. */
    static File fromDocument(Uri document) {
        return fromDocumentId(DocumentsContract.getDocumentId(document));
    }

    /**
     * External-storage document ids are {@code <volume>:<path relative to that
     * volume>}. "primary" is the built-in shared storage; any other volume is a
     * removable one mounted under /storage by its UUID.
     */
    private static File fromDocumentId(String documentId) {
        if (documentId == null) return null;
        int separator = documentId.indexOf(':');
        if (separator < 0) return null;
        String volume = documentId.substring(0, separator);
        String relative = documentId.substring(separator + 1);
        if (relative.contains("..")) return null;
        File base = "primary".equalsIgnoreCase(volume)
                ? Environment.getExternalStorageDirectory()
                : new File("/storage", volume);
        return relative.isEmpty() ? base : new File(base, relative);
    }
}
