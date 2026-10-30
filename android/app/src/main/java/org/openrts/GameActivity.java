package org.openrts;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import java.io.File;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    @Override
    protected String[] getArguments() {
        // The emulator probe is available only in debug APKs.
        return BuildConfig.DEBUG && getIntent().getBooleanExtra("sdlSmoke", false)
            ? new String[] {"--android-smoke"} : new String[0];
    }

    @Override
    protected void onCreate(Bundle state) {
        // SDL owns the rest of the Activity lifecycle and surface/audio handling.
        super.onCreate(state);
        if (!(BuildConfig.DEBUG && getIntent().getBooleanExtra("sdlSmoke", false)) &&
            !GameData.isReady(new File(getFilesDir(), BuildConfig.DATA_DIRECTORY),
                BuildConfig.DATA_DIRECTORY + "/")) finish();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // C plugins have process-global state and atexit handlers. Each launch
        // must get a fresh runtime; the launcher/import remain in the main process.
        Process.killProcess(Process.myPid());
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        // SDL 2.32's HID manager uses the two-argument API for its USB receiver.
        if (Build.VERSION.SDK_INT >= 33)
            return super.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        return super.registerReceiver(receiver, filter);
    }
}
