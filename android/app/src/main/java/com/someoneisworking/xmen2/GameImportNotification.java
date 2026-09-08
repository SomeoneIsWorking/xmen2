package com.someoneisworking.xmen2;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import io.github.someoneisworking.lucent.LucentImportNotification;

/** Title wording and setup destination; Lucent owns foreground-service lifetime. */
final class GameImportNotification {
    private static final String CHANNEL_ID = "xmen2_game_import";
    private static final int NOTIFICATION_ID = 0x5849;
    private final Context context;
    private final LucentImportNotification notification;

    GameImportNotification(Context context) {
        this.context = context;
        notification = new LucentImportNotification(context, NOTIFICATION_ID);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager manager =
                    (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Game File Installation", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Progress of game-file copy and extraction into private storage");
            channel.setShowBadge(false);
            manager.createNotificationChannel(channel);
        }
    }

    void start(String progress) {
        notification.start(build(progress));
    }

    void update(String progress) {
        notification.update(build(progress));
    }

    void stop() {
        notification.stop();
    }

    private Notification build(String progress) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(context, CHANNEL_ID) : new Notification.Builder(context);
        Intent tap = new Intent(context, XMen2SetupActivity.class);
        tap.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        return builder.setContentTitle("Installing X-Men Legends II")
                .setContentText(progress)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setOngoing(true)
                .setProgress(0, 0, true)
                .setContentIntent(PendingIntent.getActivity(context, 0, tap, flags))
                .build();
    }
}
