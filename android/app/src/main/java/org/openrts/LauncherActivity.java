package org.openrts;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public final class LauncherActivity extends Activity {
    private static final int IMPORT_DATA = 1;
    // Process-owned import survives Activity recreation without retaining its views.
    private static volatile boolean importing;
    private static volatile String message;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private Button play, importData;
    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            boolean busy = importing;
            importData.setEnabled(!busy);
            play.setEnabled(!busy && ready());
            if (message != null) status.setText(message);
            if (busy) handler.postDelayed(this, 250);
        }
    };

    private boolean ready() {
        return GameData.isReady(new File(getFilesDir(), BuildConfig.DATA_DIRECTORY),
            BuildConfig.DATA_DIRECTORY + "/");
    }

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        status = new TextView(this);
        status.setText("Supply your retail game files as a ZIP containing " +
            BuildConfig.DATA_DIRECTORY + "/. File names must keep their original case.\n\n" +
            "Touch acts as the left mouse button. A mouse and keyboard are recommended. " +
            "You can import again to replace incomplete or installed data.");
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
        play.setOnClickListener(view -> {
            if (!importing && ready()) startActivity(new Intent(this, GameActivity.class));
        });
        layout.addView(play);
        setContentView(layout);
    }

    @Override protected void onResume() {
        super.onResume();
        refresh.run();
    }

    @Override protected void onPause() {
        handler.removeCallbacks(refresh);
        super.onPause();
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != IMPORT_DATA || result != RESULT_OK || data == null || importing) return;
        Uri uri = data.getData();
        if (uri == null) return;
        Context context = getApplicationContext();
        message = "Importing game data…";
        importing = true;
        new Thread(() -> importZip(context, uri), "Import game data").start();
        refresh.run();
    }

    private static void importZip(Context context, Uri uri) {
        File root = new File(context.getFilesDir(), BuildConfig.DATA_DIRECTORY);
        File staging = new File(context.getFilesDir(), "import");
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("Cannot open ZIP.");
            GameData.install(input, BuildConfig.DATA_DIRECTORY + "/", staging, root);
            message = "Import verified. Game data is ready for startup.";
        } catch (IOException | SecurityException error) {
            message = "Import failed: " + error.getMessage();
        } finally { importing = false; }
    }
}
