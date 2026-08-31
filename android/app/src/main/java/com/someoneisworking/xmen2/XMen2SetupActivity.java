package com.someoneisworking.xmen2;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import io.github.someoneisworking.lucent.LucentDocumentImport;

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
    private static final String SOURCE_PATH = "source-path";
    private static final String INSTALL_DIRECTORY = "game";
    private static final int MAXIMUM_ENTRIES = 100_000;
    private static final long MAXIMUM_IMPORT_BYTES = 4L * 1024L * 1024L * 1024L;

    static {
        System.loadLibrary("main");
    }

    private static native boolean nativeConfigureStorage(String dataDirectory,
                                                         String installSource);
    private static native boolean nativeValidateInstall(String installSource,
                                                        String archiveDestination);

    private TextView status;
    private LinearLayout choices;
    private LucentDocumentImport importer;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        importer = new LucentDocumentImport(
                this, new LucentDocumentImport.Limits(MAXIMUM_ENTRIES,
                                                       MAXIMUM_IMPORT_BYTES,
                                                       64 * 1024));
        importer.cleanStaleImports();
        buildLayout();
        try {
            copyAssetTree("ui", new File(getFilesDir(), "ui"));
        } catch (IOException error) {
            showError("Could not prepare the built-in UI: " + error.getMessage());
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        String saved = getPreferences(MODE_PRIVATE).getString(SOURCE_PATH, null);
        if (saved != null && acceptStoredSource(new File(saved))) return;
        showChoices();
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) importer.cancel();
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

        choices = new LinearLayout(this);
        choices.setOrientation(LinearLayout.HORIZONTAL);
        choices.setGravity(Gravity.CENTER);
        layout.addView(choices, new LinearLayout.LayoutParams(-1, -2));

        Button folder = new Button(this);
        folder.setText("Choose install folder");
        folder.setOnClickListener(view -> openFolderPicker());
        choices.addView(folder, new LinearLayout.LayoutParams(-2, -2));

        Button zip = new Button(this);
        zip.setText("Choose ZIP");
        zip.setOnClickListener(view -> openZipPicker());
        choices.addView(zip, new LinearLayout.LayoutParams(-2, -2));

        setContentView(layout);
    }

    private void showChoices() {
        status.setText("Choose the folder containing XMen2.exe, or a ZIP of your legally obtained PC install.\n\nThe game files are copied once into this app’s private storage and kept there for future launches.");
        choices.setVisibility(View.VISIBLE);
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
        choices.setVisibility(View.GONE);
        importer.pickTree(FOLDER_REQUEST, importCallback());
    }

    private void openZipPicker() {
        choices.setVisibility(View.GONE);
        importer.pickDocument(ZIP_REQUEST, importCallback());
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (!importer.handleActivityResult(request, result, data))
            super.onActivityResult(request, result, data);
    }

    // --- Handoff ---

    private File sourceFor(LucentDocumentImport.Result result, File root) {
        return result.isTree ? root : new File(root, ".x2-prepared");
    }

    /** Retains only a complete, title-validated selection. */
    private void acceptImported(LucentDocumentImport.Result result) {
        if (!result.isTree && !result.documentName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            showError("That is not a ZIP archive. Choose a ZIP, or use \"Choose install folder\".");
            return;
        }
        File pickedSource = result.isTree ? result.stagingDirectory
                : new File(result.stagingDirectory, result.documentName);
        File stagedSource = sourceFor(result, result.stagingDirectory);
        if (!nativeValidateInstall(pickedSource.getAbsolutePath(),
                                   result.isTree ? "" : stagedSource.getAbsolutePath())) {
            showError("That is not a usable X-Men Legends II install.");
            return;
        }
        try {
            File installed = importer.promoteValidated(result, INSTALL_DIRECTORY);
            File source = sourceFor(result, installed);
            if (!configureNative(source)) {
                showError("Could not retain the selected X-Men Legends II install.");
                return;
            }
            getPreferences(MODE_PRIVATE).edit()
                    .putString(SOURCE_PATH, source.getAbsolutePath()).apply();
            startGame();
        } catch (IOException error) {
            showError("Could not retain the selected game files: " + error.getMessage());
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
        return nativeConfigureStorage(getFilesDir().getAbsolutePath(), source.getAbsolutePath());
    }

    private void startGame() {
        startActivity(new Intent(this, XMen2GameActivity.class));
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
        if (status != null) status.setText(message);
        if (choices != null) choices.setVisibility(View.VISIBLE);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
