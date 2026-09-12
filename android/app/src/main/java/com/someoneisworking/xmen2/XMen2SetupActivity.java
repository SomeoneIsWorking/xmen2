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

import io.github.someoneisworking.lucent.LucentDocumentImport;
import io.github.someoneisworking.lucent.LucentImportProgress;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/** Android-owned first-run setup. Native code retains title validation. */
public final class XMen2SetupActivity extends Activity {
    private static final int FOLDER_REQUEST = 0x5846;
    private static final int ZIP_REQUEST = 0x5847;
    private static final String PICKER_STATE = "lucent-picker";
    private static final String SOURCE_PATH = "source-path";
    private static final String TRACE_FILES = "com.someoneisworking.xmen2.trace.files";
    private static final String TRACE_PERFORMANCE =
            "com.someoneisworking.xmen2.trace.performance";
    private static final String TRACE_DRAW_DUMP = "com.someoneisworking.xmen2.trace.draw_dump";
    private static final String INSTALL_DIRECTORY = "game";
    private static final int MAXIMUM_ENTRIES = 100_000;
    private static final long MAXIMUM_IMPORT_BYTES = 4L * 1024L * 1024L * 1024L;

    static {
        System.loadLibrary("main");
    }

    private static native boolean nativeConfigureStorage(String dataDirectory,
                                                         String installSource,
                                                         boolean traceFiles,
                                                         boolean tracePerformance,
                                                         boolean traceDrawDump,
                                                         String bootMap);
    private static native boolean nativeValidateInstall(String installSource,
                                                        String archiveDestination);

    private TextView status;
    private ProgressBar progressBar;
    private LinearLayout choices;
    private LucentDocumentImport importer;
    private LucentImportProgress importNotification;
    private long importedEntries;
    private long importedBytes;
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
            Log.i("XMen2", "debug setup: performance=" + tracePerformance
                    + " drawDump=" + traceDrawDump + " bootMap=" + bootMap);
        }
        importNotification = new LucentImportProgress(this, 0x5849, "xmen2_game_import",
                "Game File Installation", "Installing X-Men Legends II", XMen2SetupActivity.class);
        importer = new LucentDocumentImport(
                this, new LucentDocumentImport.Limits(MAXIMUM_ENTRIES,
                                                       MAXIMUM_IMPORT_BYTES,
                                                       64 * 1024));
        /* Lucent owns the copy and reports what it has done; the wording is
           this screen's. A late update can arrive just after the import
           finished, because its post was already in flight -- so it is only
           drawn while the import still owns the screen. */
        importer.setProgressListener((entries, bytes, currentName) -> {
            importedEntries = entries;
            importedBytes = bytes;
            importingName = currentName;
            if (importer.active() && status != null) {
                status.setText(importingText());
                importNotification.update(importingText());
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
        String saved = getPreferences(MODE_PRIVATE).getString(SOURCE_PATH, null);
        if (saved != null && acceptStoredSource(new File(saved))) return;
        /* This runs again the moment the Android file picker closes, while the
           copy the player just started is still running. Offering the Browse
           buttons here is what makes the screen look idle mid-import: the next
           tap is refused with "A game-file import is already active", which
           reads as a stuck app rather than as the copy it actually is. */
        if (importer.active()) {
            showImporting();
            return;
        }
        showChoices();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBundle(PICKER_STATE, importer.savePickerState());
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) {
            importer.cancel();
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

        Button executable = new Button(this);
        executable.setText("Browse for XMen2.exe");
        executable.setOnClickListener(view -> openFolderPicker());
        choices.addView(executable, new LinearLayout.LayoutParams(-2, -2));

        Button zip = new Button(this);
        zip.setText("Choose ZIP");
        zip.setOnClickListener(view -> openZipPicker());
        choices.addView(zip, new LinearLayout.LayoutParams(-2, -2));

        setContentView(layout);
    }

    /* A whole PC install is gigabytes over SAF, so this state can last
       minutes. It says what is happening and takes the buttons away, because
       the only thing a second tap can do is be refused. */
    private void showImporting() {
        status.setText(importingText());
        choices.setVisibility(View.GONE);
        if (progressBar != null) {
            progressBar.setVisibility(View.VISIBLE);
        }
        importNotification.start(importingText());
    }

    /* There is no total to count towards: Android enumerates a picked folder
       as it is walked, so how much is left is unknown until it has been read.
       What CAN be said honestly is how much has arrived and what it is on,
       and a number that keeps moving is the difference between waiting and
       wondering whether it died. */
    private String importingText() {
        StringBuilder text = new StringBuilder(
                "Copying the game files into this app\u2019s private storage.");
        if (importedEntries > 0) {
            text.append("\n\n").append(importedEntries).append(
                    importedEntries == 1 ? " file, " : " files, ")
                .append(formatBytes(importedBytes));
            if (importingName != null && !importingName.isEmpty()) {
                text.append("\n").append(importingName);
            }
        }
        text.append("\n\nA full install takes several minutes \u2014 progress is also shown in your notifications.");
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

    private void showChoices() {
        status.setText("Browse to the folder containing XMen2.exe, or choose a ZIP of your legally obtained PC install. Android needs the whole install folder so it can validate and copy the required game files.\n\nThe game files are copied once into this app’s private storage and kept there for future launches.");
        choices.setVisibility(View.VISIBLE);
        if (progressBar != null) {
            progressBar.setVisibility(View.GONE);
        }
        importNotification.stop();
    }

    // --- Pickers ---

    private LucentDocumentImport.Callback importCallback() {
        return new LucentDocumentImport.Callback() {
            @Override
            public void onImported(LucentDocumentImport.Result result) {
                acceptImported(result);
            }

            @Override
            public void onCancelled() {
                showChoices();
            }

            @Override
            public void onFailed(String message) {
                showError(message);
            }
        };
    }

    private void openFolderPicker() {
        resetProgress();
        choices.setVisibility(View.GONE);
        importer.pickTree(FOLDER_REQUEST, importCallback());
    }

    private void openZipPicker() {
        resetProgress();
        choices.setVisibility(View.GONE);
        importer.pickDocument(ZIP_REQUEST, importCallback());
    }

    private void resetProgress() {
        importedEntries = 0;
        importedBytes = 0;
        importingName = null;
    }

    /** Drives the native product through an app-private debug source, never the release picker. */
    private boolean launchDebugPrivateInstall() {
        File source = XMen2DebugInstall.requestedSource(this);
        if (source == null) {
            return false;
        }
        if (!nativeValidateInstall(source.getAbsolutePath(), "")) {
            showError("The debug private install is not a usable X-Men Legends II install.");
            return true;
        }
        if (!configureNative(source)) {
            showError("Could not configure the debug private install.");
            return true;
        }
        startGame();
        return true;
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (!importer.handleActivityResult(request, result, data)) {
            super.onActivityResult(request, result, data);
            return;
        }
        /* The copy runs off the UI thread, so this returns immediately and
           the screen has to say what is now happening. */
        if (importer.active())
            showImporting();
    }

    // --- Handoff ---

    private File sourceFor(LucentDocumentImport.Result result, File root) {
        return result.isTree ? root : new File(root, ".x2-prepared");
    }

    /** Retains only a complete, title-validated selection. */
    private void acceptImported(LucentDocumentImport.Result result) {
        if (!result.isTree && !result.documentName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            showError("That is not a ZIP archive. Choose a ZIP, or use \"Browse for XMen2.exe\"."
                    + discardRejectedImport(result));
            return;
        }
        File pickedSource = result.isTree ? result.stagingDirectory
                : new File(result.stagingDirectory, result.documentName);
        File stagedSource = sourceFor(result, result.stagingDirectory);
        if (!nativeValidateInstall(pickedSource.getAbsolutePath(),
                                   result.isTree ? "" : stagedSource.getAbsolutePath())) {
            showError("That is not a usable X-Men Legends II install."
                    + discardRejectedImport(result));
            return;
        }
        try {
            if (!result.isTree) importer.discardValidatedDocument(result);
            File installed = importer.promoteValidated(result, INSTALL_DIRECTORY);
            File source = sourceFor(result, installed);
            if (!configureNative(source)) {
                showError("Could not retain the selected X-Men Legends II install.");
                return;
            }
            getPreferences(MODE_PRIVATE).edit()
                    .putString(SOURCE_PATH, source.getAbsolutePath()).apply();
            importNotification.stop();
            startGame();
        } catch (IOException error) {
            importNotification.stop();
            showError("Could not retain the selected game files: " + error.getMessage());
        }
    }

    private String discardRejectedImport(LucentDocumentImport.Result result) {
        try {
            importer.discard(result);
            return "";
        } catch (IOException error) {
            String detail = error.getMessage();
            return " Android could not discard its private staging"
                    + (detail == null ? "." : ": " + detail);
        }
    }

    private boolean acceptStoredSource(File source) {
        File privateSource = privateInstallSource(source);
        if (privateSource == null || !privateSource.exists()) return false;
        if (!nativeValidateInstall(privateSource.getAbsolutePath(), "") ||
            !configureNative(privateSource)) return false;
        startGame();
        return true;
    }

    /** Resolves Android's equivalent data-directory aliases before enforcing containment. */
    private File privateInstallSource(File source) {
        try {
            File root = new File(getFilesDir(), INSTALL_DIRECTORY).getCanonicalFile();
            File candidate = source.getCanonicalFile();
            return candidate.toPath().startsWith(root.toPath()) ? candidate : null;
        } catch (IOException error) {
            return null;
        }
    }

    private boolean configureNative(File source) {
        return nativeConfigureStorage(getFilesDir().getAbsolutePath(), source.getAbsolutePath(),
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
