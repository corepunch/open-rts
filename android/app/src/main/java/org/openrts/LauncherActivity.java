package org.openrts;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public final class LauncherActivity extends Activity {
    private static final int IMPORT_DATA = 1;
    private TextView status;
    private Button play, importData;

    @Override
    public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        status = new TextView(this);
        status.setText("Supply your retail game files as a ZIP containing " +
            BuildConfig.DATA_DIRECTORY + "/. File names must keep their original case.\n\n" +
            "Touch acts as the left mouse button. A mouse and keyboard are recommended.");
        layout.addView(status);
        importData = new Button(this);
        importData.setText("Import game data ZIP");
        importData.setOnClickListener(view -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            startActivityForResult(intent, IMPORT_DATA);
        });
        layout.addView(importData);
        play = new Button(this);
        play.setText("Play");
        play.setOnClickListener(view -> startActivity(new Intent(this, GameActivity.class)));
        layout.addView(play);
        setContentView(layout);
    }

    @Override
    protected void onResume() {
        super.onResume();
        play.setEnabled(new File(getFilesDir(), BuildConfig.DATA_DIRECTORY).isDirectory());
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != IMPORT_DATA || result != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;
        importData.setEnabled(false);
        play.setEnabled(false);
        status.setText("Importing game data…");
        new Thread(() -> {
            String message;
            try {
                importZip(uri);
                message = "Game data is ready.";
            } catch (IOException | SecurityException error) {
                message = "Import failed: " + error.getMessage();
            }
            String text = message;
            runOnUiThread(() -> {
                status.setText(text);
                importData.setEnabled(true);
                play.setEnabled(new File(getFilesDir(), BuildConfig.DATA_DIRECTORY).isDirectory());
            });
        }, "Import game data").start();
    }

    private void importZip(Uri uri) throws IOException {
        File root = new File(getFilesDir(), BuildConfig.DATA_DIRECTORY);
        File staging = new File(getCacheDir(), "import");
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("Cannot open ZIP.");
            GameData.install(input, BuildConfig.DATA_DIRECTORY + "/", staging, root);
        }
    }
}
