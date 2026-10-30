package org.openrts;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    @Override
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        // SDL 2.32's HID manager uses the two-argument API for its USB receiver.
        if (Build.VERSION.SDK_INT >= 33)
            return super.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        return super.registerReceiver(receiver, filter);
    }
}
