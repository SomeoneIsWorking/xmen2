package com.someoneisworking.xmen2;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import io.github.someoneisworking.android.AndroidDocumentImport;
import io.github.someoneisworking.android.AndroidDocumentTree;
import io.github.someoneisworking.android.AndroidImportProgress;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * Android-owned first-run setup. The game and its saves live in a folder the player picks in
 * shared storage, so they survive an uninstall; native code retains title validation.
 */
public final class XMen2SetupActivity extends Activity {
    private static final String TAG = "XMen2";
    private static final int ZIP_REQUEST = 0x5847;
    private static final int FOLDER_REQUEST = 0x584A;
    private static final String PICKER_STATE = "xmen2-picker";
    private static final String FOLDER_PICK_EXISTING = "xmen2-folder-existing";
    private static final String TRACE_FILES = "com.someoneisworking.xmen2.trace.files";
    private static final String TRACE_PERFORMANCE =
            "com.someoneisworking.xmen2.trace.performance";
    private static final String TRACE_DRAW_DUMP = "com.someoneisworking.xmen2.trace.draw_dump";
    private static final String FOLDER_KEY = "install-folder";
    private static final String SUGGESTED_FOLDER = "Documents";
    private static final String GAME_LEAF = "game";
    private static final String SAVES_LEAF = "saves";
    /* The package-storage install every release before the shared folder used. */
    private static final String RETIRED_INSTALL = "game";
    private static final String PREPARED_LEAF = ".x2-prepared";
    private static final int MAXIMUM_ENTRIES = 100_000;
    private static final long MAXIMUM_IMPORT_BYTES = 4L * 1024L * 1024L * 1024L;

    static {
        System.loadLibrary("main");
    }

    private static native boolean nativeConfigureStorage(String dataDirectory,
                                                         String installSource,
                                                         String saveDirectory,
                                                         boolean traceFiles,
                                                         boolean tracePerformance,
                                                         boolean traceDrawDump,
                                                         String bootMap);
    private static native String nativeValidateInstall(String installSource,
                                                       String archiveDestination);
    private static native String nativeMountInstallFolder(AndroidDocumentTree folder);

    private TextView status;
    private ProgressBar progressBar;
    private LinearLayout choices;
    private AndroidDocumentImport importer;
    private AndroidImportProgress importNotification;
    private AndroidDocumentTree folder;
    private boolean pickingExistingFolder;
    private boolean copyingToFolder;
    private long importedEntries;
    private long importedBytes;
    private long importTotalBytes;
    private String importingName;
    private boolean traceFiles;
    private boolean tracePerformance;
    private boolean traceDrawDump;
    private boolean gpuSelftest;
    private String bootMap;
    private boolean invalidBootMap;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (BuildConfig.DEBUG) {
            traceFiles = getIntent().getBooleanExtra(TRACE_FILES, false);
            tracePerformance = getIntent().getBooleanExtra(TRACE_PERFORMANCE, false);
            traceDrawDump = getIntent().getBooleanExtra(TRACE_DRAW_DUMP, false);
            gpuSelftest = getIntent().getBooleanExtra(XMen2GameActivity.GPU_SELFTEST, false);
            String requestedBootMap = getIntent().getStringExtra(XMen2GameActivity.BOOT_MAP);
            bootMap = validatedBootMap(requestedBootMap);
            invalidBootMap = requestedBootMap != null && !requestedBootMap.isEmpty()
                    && bootMap == null;
            Log.i(TAG, "debug setup: performance=" + tracePerformance
                    + " drawDump=" + traceDrawDump + " bootMap=" + bootMap);
        }
        pickingExistingFolder = state != null && state.getBoolean(FOLDER_PICK_EXISTING);
        importNotification = new AndroidImportProgress(this, 0x5849, "xmen2_game_import",
                "Game File Installation", "Installing X-Men Legends II", XMen2SetupActivity.class);
        importer = new AndroidDocumentImport(
                this, importStorageRoot(),
                new AndroidDocumentImport.Limits(MAXIMUM_ENTRIES,
                                                MAXIMUM_IMPORT_BYTES,
                                                256 * 1024));
        /* The shared Android framework owns the copy and reports what it has done; the wording is
           this screen's. A late update can arrive just after the import
           finished, because its post was already in flight -- so it is only
           drawn while the import still owns the screen. */
        importer.setProgressListener((entries, bytes, totalBytes, currentName) -> {
            if (importer.active() && status != null) {
                noteProgress(entries, bytes, totalBytes, currentName);
            }
        });
        importer.restorePickerState(state == null ? null : state.getBundle(PICKER_STATE), importCallback());
        importer.cleanStaleImports();
        buildLayout();
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                    new String[] { android.Manifest.permission.POST_NOTIFICATIONS }, 0x5848);
            }
        }
        try {
            copyAssetTree("ui", new File(getFilesDir(), "ui"));
        } catch (IOException error) {
            showError("Could not prepare the built-in UI: " + error.getMessage());
            return;
        }
        if (launchDebugPrivateInstall()) {
            return;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (invalidBootMap) {
            showError("The debug boot map contains an unsafe or invalid map name.");
            return;
        }
        /* This runs again the moment a picker closes, while the copy the
           player just started is still running. Offering the buttons here is
           what makes the screen look idle mid-import. */
        if (importer.active() || AndroidDocumentTree.publishing()) {
            showImporting();
            return;
        }
        AndroidDocumentTree remembered = AndroidDocumentTree.remembered(this, FOLDER_KEY);
        if (remembered == null) {
            showChoices();
            return;
        }
        if (!startFromFolder(remembered)) {
            showFolderWithoutInstall("The install folder holds no usable X-Men Legends II "
                    + "install yet. Choose the ZIP of your PC install to put the game there.");
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBundle(PICKER_STATE, importer.savePickerState());
        state.putBoolean(FOLDER_PICK_EXISTING, pickingExistingFolder);
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) {
            importer.cancel();
            if (folder != null) folder.cancelPublication();
            importNotification.stop();
        }
        super.onDestroy();
    }

    // --- Screens ---

    private void buildLayout() {
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
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(-1, -2);
        textParams.setMargins(0, padding / 2, 0, padding / 2);
        layout.addView(status, textParams);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setIndeterminate(true);
        progressBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(-1, -2);
        progressParams.setMargins(padding, 0, padding, padding / 2);
        layout.addView(progressBar, progressParams);

        choices = new LinearLayout(this);
        choices.setOrientation(LinearLayout.HORIZONTAL);
        choices.setGravity(Gravity.CENTER);
        layout.addView(choices, new LinearLayout.LayoutParams(-1, -2));
        setFolderChoices();

        setContentView(layout);
    }

    private void setChoices(String firstLabel, View.OnClickListener first,
                            String secondLabel, View.OnClickListener second) {
        choices.removeAllViews();
        for (int index = 0; index < 2; ++index) {
            Button button = new Button(this);
            button.setText(index == 0 ? firstLabel : secondLabel);
            button.setOnClickListener(index == 0 ? first : second);
            choices.addView(button, new LinearLayout.LayoutParams(-2, -2));
        }
        choices.setVisibility(View.VISIBLE);
        if (progressBar != null) progressBar.setVisibility(View.GONE);
    }

    private void showChoices() {
        status.setText("Choose a folder for X-Men Legends II, for example Documents/X-Men Legends 2."
                + " The game and your saves are kept there, so they survive reinstalling the app."
                + " After a reinstall, choose the same folder with Use existing install folder.");
        setFolderChoices();
        importNotification.stop();
    }

    private void setFolderChoices() {
        setChoices("New install folder", view -> openFolderPicker(false),
                   "Use existing install folder", view -> openFolderPicker(true));
    }

    private void showFolderWithoutInstall(String message) {
        status.setText(message);
        setChoices("Choose ZIP", view -> openZipPicker(),
                   "Choose another folder", view -> openFolderPicker(true));
        importNotification.stop();
    }

    /* A whole PC install is gigabytes, so this state can last minutes. It
       says what is happening and takes the buttons away, because the only
       thing a second tap can do is be refused. */
    private void showImporting() {
        status.setText(importingText());
        choices.setVisibility(View.GONE);
        if (progressBar != null) {
            progressBar.setVisibility(View.VISIBLE);
        }
        importNotification.start(importingText(), importedBytes, importTotalBytes);
        updateProgressPresentation();
    }

    private void noteProgress(long entries, long bytes, long totalBytes, String currentName) {
        importedEntries = entries;
        importedBytes = bytes;
        importTotalBytes = totalBytes;
        importingName = currentName;
        updateProgressPresentation();
    }

    private void updateProgressPresentation() {
        String text = importingText();
        status.setText(text);
        if (progressBar != null) {
            if (importTotalBytes > 0) {
                progressBar.setIndeterminate(false);
                progressBar.setMax(1000);
                progressBar.setProgress((int) Math.min(1000,
                        Math.max(0, importedBytes * 1000.0 / importTotalBytes)));
            } else {
                progressBar.setIndeterminate(true);
            }
        }
        importNotification.update(text, importedBytes, importTotalBytes);
    }

    private String importingText() {
        StringBuilder text = new StringBuilder(copyingToFolder
                ? "Copying the game into the install folder."
                : "Copying the ZIP into game storage.");
        if (importedEntries > 0) {
            text.append("\n\n").append(importedEntries).append(
                    importedEntries == 1 ? " file, " : " files, ")
                .append(formatBytes(importedBytes));
            if (importTotalBytes > 0) {
                text.append(" of ").append(formatBytes(importTotalBytes));
                text.append(" (").append(String.format(Locale.US, "%.1f%%",
                        importedBytes * 100.0 / importTotalBytes)).append(")");
            }
            if (importingName != null && !importingName.isEmpty()) {
                text.append("\n").append(importingName);
            }
        }
        if (!copyingToFolder) {
            text.append("\n\nAn interrupted copy can resume when you choose the same ZIP again.");
        }
        return text.toString();
    }

    private static String formatBytes(long bytes) {
        if (bytes < 1024L) {
            return bytes + " B";
        }
        if (bytes < 1024L * 1024L) {
            return String.format(Locale.US, "%.0f KB", bytes / 1024.0);
        }
        if (bytes < 1024L * 1024L * 1024L) {
            return String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0));
        }
        return String.format(Locale.US, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }

    // --- Pickers ---

    private AndroidDocumentImport.Callback importCallback() {
        return new AndroidDocumentImport.Callback() {
            @Override
            public void onImported(AndroidDocumentImport.Result result) {
                acceptImported(result);
            }

            @Override
            public void onCancelled() {
                showFolderWithoutInstall("Choose the ZIP of your PC install to put the game in"
                        + " the install folder.");
            }

            @Override
            public void onFailed(String message) {
                showError(message);
            }
        };
    }

    private void openFolderPicker(boolean existing) {
        pickingExistingFolder = existing;
        if (!AndroidDocumentTree.pick(this, FOLDER_REQUEST, SUGGESTED_FOLDER)) {
            showError("No Android folder picker is available.");
        }
    }

    private void openZipPicker() {
        resetProgress();
        choices.setVisibility(View.GONE);
        importer.pickDocument(ZIP_REQUEST, importCallback());
    }

    private void resetProgress() {
        importedEntries = 0;
        importedBytes = 0;
        importTotalBytes = 0;
        importingName = null;
    }

    /** Drives the native product through an app-private debug source, never the release picker. */
    private boolean launchDebugPrivateInstall() {
        File source = XMen2DebugInstall.requestedSource(this);
        if (source == null) {
            return false;
        }
        if (nativeValidateInstall(source.getAbsolutePath(), "") == null) {
            showError("The debug private install is not a usable X-Men Legends II install.");
            return true;
        }
        if (!configureNative(source.getAbsolutePath(), null)) {
            showError("Could not configure the debug private install.");
            return true;
        }
        startGame();
        return true;
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == FOLDER_REQUEST) {
            acceptPickedFolder(result, data);
            return;
        }
        if (!importer.handleActivityResult(request, result, data)) {
            super.onActivityResult(request, result, data);
            return;
        }
        /* The copy runs off the UI thread, so this returns immediately and
           the screen has to say what is now happening. */
        if (importer.active())
            showImporting();
    }

    // --- Install folder ---

    private void acceptPickedFolder(int result, Intent data) {
        AndroidDocumentTree picked;
        try {
            picked = AndroidDocumentTree.accept(this, result, data, FOLDER_KEY);
        } catch (IOException error) {
            showError("Could not keep access to that folder: " + error.getMessage());
            return;
        }
        if (picked == null) {
            showChoices();
            return;
        }
        if (startFromFolder(picked)) return;
        if (pickingExistingFolder) {
            showFolderWithoutInstall("That folder holds no usable X-Men Legends II install."
                    + " Choose the ZIP of your PC install to put the game there, or choose"
                    + " another folder.");
        } else {
            openZipPicker();
        }
    }

    /** Validates the folder's game in place and starts it; false when there is none. */
    private boolean startFromFolder(AndroidDocumentTree candidate) {
        folder = candidate;
        String root = nativeMountInstallFolder(candidate);
        if (root == null) {
            showError("Could not open the install folder.");
            return true;
        }
        String executable = root + "/" + GAME_LEAF + "/XMen2.exe";
        if (nativeValidateInstall(executable, "") == null) {
            return false;
        }
        Log.i(TAG, "install folder " + candidate.uri() + ": validated in place, no copy");
        discardRetiredInstall();
        if (!configureNative(executable, root + "/" + SAVES_LEAF)) {
            showError("Could not use the install folder.");
            return true;
        }
        startGame();
        return true;
    }

    /** Copies a title-validated ZIP extraction into the install folder. */
    private void acceptImported(AndroidDocumentImport.Result result) {
        if (!result.documentName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            showError("That is not a ZIP archive. Choose a ZIP from your PC install."
                    + discardRejectedImport(result));
            return;
        }
        File archive = new File(result.stagingDirectory, result.documentName);
        File prepared = new File(result.stagingDirectory, PREPARED_LEAF);
        String directory = nativeValidateInstall(archive.getAbsolutePath(),
                                                 prepared.getAbsolutePath());
        if (directory == null) {
            showError("That is not a usable X-Men Legends II install."
                    + discardRejectedImport(result));
            return;
        }
        if (folder == null) folder = AndroidDocumentTree.remembered(this, FOLDER_KEY);
        if (folder == null) {
            showError("Choose the install folder before the ZIP." + discardRejectedImport(result));
            return;
        }
        try {
            importer.discardValidatedDocument(result);
        } catch (IOException error) {
            showError("Could not free the copied ZIP: " + error.getMessage()
                    + discardRejectedImport(result));
            return;
        }
        resetProgress();
        copyingToFolder = true;
        showImporting();
        folder.publish(new File(directory), GAME_LEAF, this::noteProgress,
                new AndroidDocumentTree.PublishCallback() {
                    @Override
                    public void onPublished() {
                        copyingToFolder = false;
                        String discarded = discardRejectedImport(result);
                        if (!discarded.isEmpty()) Log.w(TAG, discarded);
                        if (!startFromFolder(folder)) {
                            showError("The copied game in the install folder did not validate.");
                        }
                    }

                    @Override
                    public void onFailed(String message) {
                        copyingToFolder = false;
                        showError("Could not copy the game into the install folder: " + message
                                + discardRejectedImport(result));
                    }
                });
    }

    private String discardRejectedImport(AndroidDocumentImport.Result result) {
        try {
            importer.discard(result);
            return "";
        } catch (IOException error) {
            String detail = error.getMessage();
            return " Android could not discard its private staging"
                    + (detail == null ? "." : ": " + detail);
        }
    }

    /* No migration: a package-storage install is a one-time re-import, and
       its gigabytes go once the folder install is in use. */
    private void discardRetiredInstall() {
        try {
            importer.discardPublished(RETIRED_INSTALL);
        } catch (IOException error) {
            Log.w(TAG, "could not discard the retired package-storage install", error);
        }
    }

    private File importStorageRoot() {
        File obb = getObbDir();
        if (obb != null && (obb.isDirectory() || obb.mkdirs())) return obb;
        return getFilesDir();
    }

    private boolean configureNative(String source, String saveDirectory) {
        return nativeConfigureStorage(getFilesDir().getAbsolutePath(), source, saveDirectory,
                                      traceFiles, tracePerformance, traceDrawDump, bootMap);
    }

    /** Accept map names, never paths or shell-like values, from debug launchers only. */
    private static String validatedBootMap(String requested) {
        if (requested == null || requested.isEmpty() || requested.length() > 128
                || requested.startsWith("/") || requested.contains("..")) {
            return null;
        }
        for (int index = 0; index < requested.length(); index++) {
            char value = requested.charAt(index);
            if (!(Character.isLetterOrDigit(value) || value == '/' || value == '_'
                    || value == '-' || value == '.')) {
                return null;
            }
        }
        return requested;
    }

    private void startGame() {
        Intent game = new Intent(this, XMen2GameActivity.class);
        if (BuildConfig.DEBUG && gpuSelftest) {
            game.putExtra(XMen2GameActivity.GPU_SELFTEST, true);
        }
        startActivity(game);
        finish();
    }

    // --- Bundled UI assets ---

    private void copyAssetTree(String assetPath, File destination) throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children == null || children.length == 0) {
            try (InputStream input = getAssets().open(assetPath);
                 OutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[64 * 1024];
                for (int count; (count = input.read(buffer)) >= 0; ) {
                    if (count > 0) output.write(buffer, 0, count);
                }
            }
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs())
            throw new IOException("could not create UI directory");
        for (String child : children) {
            if (child.indexOf('/') >= 0 || child.indexOf('\\') >= 0)
                throw new IOException("unsafe bundled asset name: " + child);
            copyAssetTree(assetPath + "/" + child, new File(destination, child));
        }
    }

    private void showError(String message) {
        importNotification.stop();
        if (status != null) status.setText(message);
        if (choices != null) choices.setVisibility(View.VISIBLE);
        if (progressBar != null) progressBar.setVisibility(View.GONE);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
