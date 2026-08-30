package com.someoneisworking.xmen2;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
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

/** Android-owned first-run setup. Native code only validates the chosen install. */
public final class XMen2SetupActivity extends Activity {
    private static final int FOLDER_REQUEST = 0x5846;
    private static final int ZIP_REQUEST = 0x5847;
    private static final String SOURCE_PATH = "source-path";

    static {
        System.loadLibrary("main");
    }

    private static native boolean nativeConfigureStorage(String dataDirectory,
                                                          String installSource);

    private TextView status;
    private LinearLayout choices;
    private Button grant;
    /** onResume runs after onActivityResult, and must not erase its message. */
    private boolean rejectionShown;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
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
        /* Storage access is granted in Settings, outside this Activity, so the
           gate is re-evaluated on every return rather than only at creation. */
        if (!Environment.isExternalStorageManager()) {
            showPermissionRequest();
            return;
        }
        if (rejectionShown) {
            rejectionShown = false;
            choices.setVisibility(View.VISIBLE);
            grant.setVisibility(View.GONE);
            return;
        }
        String saved = getPreferences(MODE_PRIVATE).getString(SOURCE_PATH, null);
        if (saved != null && new File(saved).exists() && acceptSource(new File(saved))) return;
        showChoices();
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

        grant = new Button(this);
        grant.setText("Grant file access");
        grant.setVisibility(View.GONE);
        grant.setOnClickListener(view -> openStorageSettings());
        layout.addView(grant, new LinearLayout.LayoutParams(-2, -2));

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

    private void showPermissionRequest() {
        status.setText("X-Men Legends II reads your PC install where it already is, so it needs permission to access your files.\n\nTap below, then turn on \"Allow access to manage all files\".");
        grant.setVisibility(View.VISIBLE);
        choices.setVisibility(View.GONE);
    }

    private void showChoices() {
        status.setText("Choose the folder containing XMen2.exe, or a ZIP of your legally obtained PC install.\n\nThe game is read where it is; nothing is copied.");
        grant.setVisibility(View.GONE);
        choices.setVisibility(View.VISIBLE);
    }

    // --- Pickers ---

    private void openStorageSettings() {
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                   Uri.parse("package:" + getPackageName()));
        try {
            startActivity(intent);
        } catch (ActivityNotFoundException error) {
            /* Some builds only offer the device-wide list. */
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (ActivityNotFoundException fallback) {
                showError("This device has no all-files-access setting to open.");
            }
        }
    }

    /**
     * The install is a folder, so the folder picker is the only selection that
     * can produce one: a single-document URI names one file, not its siblings.
     */
    private void openFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        launchPicker(intent, FOLDER_REQUEST, "No Android folder picker is available.");
    }

    private void openZipPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        /* Providers disagree on the ZIP media type -- some report
           application/octet-stream -- so the name is what gets validated. */
        intent.setType("*/*");
        launchPicker(intent, ZIP_REQUEST, "No Android document picker is available.");
    }

    private void launchPicker(Intent intent, int request, String unavailable) {
        try {
            startActivityForResult(intent, request);
        } catch (ActivityNotFoundException error) {
            showError(unavailable);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        File path = request == FOLDER_REQUEST ? InstallLocation.fromTree(uri)
                                              : InstallLocation.fromDocument(uri);
        if (path == null || !path.exists()) {
            /* Cloud and other non-file providers have no path to read in place.
               Saying so beats silently copying gigabytes at provider speed. */
            showError("That location is not a folder on this device. Choose the install from internal storage or an SD card.");
            return;
        }
        if (request == ZIP_REQUEST
                && !path.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) {
            showError("That is not a ZIP archive. Choose a ZIP, or use \"Choose install folder\".");
            return;
        }
        acceptSource(path);
    }

    // --- Handoff ---

    /** Hands the chosen install to native validation and starts the game. */
    private boolean acceptSource(File source) {
        if (!nativeConfigureStorage(getFilesDir().getAbsolutePath(),
                                    source.getAbsolutePath())) {
            showError("That is not a usable X-Men Legends II install.");
            return false;
        }
        getPreferences(MODE_PRIVATE).edit()
                .putString(SOURCE_PATH, source.getAbsolutePath()).apply();
        startActivity(new Intent(this, XMen2GameActivity.class));
        finish();
        return true;
    }

    // --- Bundled UI assets ---

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
        for (String child : children) {
            if (child.indexOf('/') >= 0 || child.indexOf('\\') >= 0)
                throw new IOException("unsafe bundled asset name: " + child);
            copyAssetTree(assetPath + "/" + child, new File(destination, child));
        }
    }

    private void showError(String message) {
        rejectionShown = true;
        if (status != null) status.setText(message);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
