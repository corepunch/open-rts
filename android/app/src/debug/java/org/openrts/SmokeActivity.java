package org.openrts;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/** Debug APK entry point for the asset-free emulator probe. Never shipped in releases. */
public final class SmokeActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        startActivity(new Intent(this, GameActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
            .putExtra("sdlSmoke", true));
        finish();
    }
}
