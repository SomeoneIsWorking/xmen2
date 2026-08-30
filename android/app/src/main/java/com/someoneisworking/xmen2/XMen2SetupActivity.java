package com.someoneisworking.xmen2;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/** Android-owned first-run setup. Native code only validates the staged install. */
public final class XMen2SetupActivity extends Activity {
    private static final int FILE_REQUEST = 0x5845;
    private static final int FOLDER_REQUEST = 0x5846;
    private static final String SOURCE = "install-source";
    private static final String SOURCE_PATH = "source-path";

    static {
        System.loadLibrary("main");
    }

    private static native boolean nativeConfigureStorage(String dataDirectory,
                                                          String installSource);

    private TextView status;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            copyAssetTree("ui", new File(getFilesDir(), "ui"));
        } catch (IOException error) {
            showError("Could not prepare the built-in UI: " + error.getMessage());
        }
        if (resumeSavedInstall()) return;
        showSetup();
    }

    private boolean resumeSavedInstall() {
        String path = getPreferences(MODE_PRIVATE).getString(SOURCE_PATH, null);
        if (path == null || !new File(path).exists()) return false;
        if (!nativeConfigureStorage(getFilesDir().getAbsolutePath(), path)) return false;
        launchGame();
        return true;
    }

    private void showSetup() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int padding = (int)(28 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);

        TextView title = new TextView(this);
        title.setText("X-Men Legends II");
        title.setTextSize(24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title, new LinearLayout.LayoutParams(-1, -2));

        status = new TextView(this);
        status.setText("Choose the folder containing XMen2.exe, or a ZIP of your legally obtained PC install.\n\nAndroid will copy the selected files into this app's private storage.");
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-1, -2);
        textParams.setMargins(0, padding / 2, 0, padding / 2);
        layout.addView(status, textParams);

        Button browse = new Button(this);
        browse.setText("Browse");
        browse.setOnClickListener(view -> openFilePicker());
        layout.addView(browse, new LinearLayout.LayoutParams(-2, -2));
        setContentView(layout);
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, FILE_REQUEST);
        } catch (ActivityNotFoundException error) {
            showError("No Android document picker is available.");
        }
    }

    private void openFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        try {
            startActivityForResult(intent, FOLDER_REQUEST);
        } catch (ActivityNotFoundException error) {
            showError("No Android folder picker is available.");
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri,
                    data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // A provider may grant a transient read permission only. The copy
            // below is still valid and is the actual lifetime we depend on.
        }
        if (request == FILE_REQUEST) handleFile(uri);
        else if (request == FOLDER_REQUEST) handleFolder(uri);
    }

    private void handleFile(Uri uri) {
        String display = displayName(uri);
        String name = (display == null ? "" : display).toLowerCase(Locale.ROOT);
        if (name.endsWith(".zip")) {
            File destination = new File(new File(getFilesDir(), SOURCE), "game.zip");
            try {
                prepareDestination(destination.getParentFile());
                copyUri(uri, destination);
                acceptSource(destination);
            } catch (IOException error) {
                showError("Could not copy the selected ZIP: " + error.getMessage());
            }
            return;
        }
        if (name.endsWith(".exe")) {
            status.setText("Now choose the install folder containing that XMen2.exe. Android grants a file URI for one file, so the folder step is required to copy the rest of the game.");
            openFolderPicker();
            return;
        }
        showError("Choose XMen2.exe or a ZIP archive.");
    }

    private void handleFolder(Uri tree) {
        File destination = new File(new File(getFilesDir(), SOURCE), "game-folder");
        try {
            prepareDestination(destination);
            copyTree(tree, DocumentsContract.getTreeDocumentId(tree), destination);
            acceptSource(destination);
        } catch (IOException error) {
            showError("Could not copy the selected install folder: " + error.getMessage());
        }
    }

    private void acceptSource(File source) {
        if (!nativeConfigureStorage(getFilesDir().getAbsolutePath(), source.getAbsolutePath())) {
            showError("The app could not initialize its private storage.");
            return;
        }
        getPreferences(MODE_PRIVATE).edit().putString(SOURCE_PATH, source.getAbsolutePath()).apply();
        launchGame();
    }

    private void launchGame() {
        startActivity(new Intent(this, XMen2GameActivity.class));
        finish();
    }

    private String displayName(Uri uri) {
        try (android.database.Cursor cursor = getContentResolver().query(
                uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        }
        return uri.toString();
    }

    private void copyTree(Uri tree, String documentId, File destination) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        try (android.database.Cursor cursor = getContentResolver().query(
                children, new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (cursor == null) throw new IOException("the provider returned no directory listing");
            while (cursor.moveToNext()) {
                String childId = cursor.getString(0);
                String name = safeName(cursor.getString(1));
                String mime = cursor.getString(2);
                Uri child = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                File output = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!output.mkdirs() && !output.isDirectory())
                        throw new IOException("could not create " + name);
                    copyTree(tree, childId, output);
                } else {
                    copyUri(child, output);
                }
            }
        }
    }

    private String safeName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") ||
                name.indexOf('/') >= 0 || name.indexOf('\\') >= 0)
            throw new IOException("the provider returned an unsafe file name");
        return name;
    }

    private void copyUri(Uri uri, File destination) throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs())
            throw new IOException("could not create staging directory");
        try (InputStream input = getContentResolver().openInputStream(uri);
             OutputStream output = new FileOutputStream(destination)) {
            if (input == null) throw new IOException("the provider returned no data");
            byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count > 0) output.write(buffer, 0, count);
            }
        }
    }

    private void copyAssetTree(String assetPath, File destination) throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children == null || children.length == 0) {
            try (InputStream input = getAssets().open(assetPath);
                 OutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) >= 0) {
                    if (count > 0) output.write(buffer, 0, count);
                }
            }
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs())
            throw new IOException("could not create UI directory");
        for (String child : children)
            copyAssetTree(assetPath + "/" + child, new File(destination, safeName(child)));
    }

    private void prepareDestination(File directory) throws IOException {
        if (directory.exists()) deleteTree(directory);
        if (!directory.mkdirs() && !directory.isDirectory())
            throw new IOException("could not create private staging directory");
    }

    private void deleteTree(File path) throws IOException {
        if (path.isDirectory()) {
            File[] children = path.listFiles();
            if (children == null) throw new IOException("could not read staging directory");
            for (File child : children) deleteTree(child);
        }
        if (!path.delete()) throw new IOException("could not replace previous selection");
    }

    private void showError(String message) {
        if (status != null) status.setText(message);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
