package com.someoneisworking.xmen2;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

import java.util.Locale;

/**
 * Foreground service providing process lifecycle protection and a persistent
 * progress notification while large game-file copies or extractions run.
 */
public final class GameImportService extends Service {
    public static final String CHANNEL_ID = "xmen2_game_import";
    public static final int NOTIFICATION_ID = 0x5849;

    private static final String ACTION_START = "com.someoneisworking.xmen2.import.START";
    private static final String ACTION_UPDATE = "com.someoneisworking.xmen2.import.UPDATE";
    private static final String ACTION_STOP = "com.someoneisworking.xmen2.import.STOP";

    private static final String EXTRA_ENTRIES = "entries";
    private static final String EXTRA_BYTES = "bytes";
    private static final String EXTRA_NAME = "name";

    private NotificationManager notificationManager;

    public static void start(Context context) {
        Intent intent = new Intent(context, GameImportService.class);
        intent.setAction(ACTION_START);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void update(Context context, long entries, long bytes, String name) {
        Intent intent = new Intent(context, GameImportService.class);
        intent.setAction(ACTION_UPDATE);
        intent.putExtra(EXTRA_ENTRIES, entries);
        intent.putExtra(EXTRA_BYTES, bytes);
        intent.putExtra(EXTRA_NAME, name);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stop(Context context) {
        Intent intent = new Intent(context, GameImportService.class);
        intent.setAction(ACTION_STOP);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopForegroundService();
            return START_NOT_STICKY;
        }

        long entries = intent.getLongExtra(EXTRA_ENTRIES, 0);
        long bytes = intent.getLongExtra(EXTRA_BYTES, 0);
        String name = intent.getStringExtra(EXTRA_NAME);

        Notification notification = buildNotification(entries, bytes, name);
        try {
            if (Build.VERSION.SDK_INT >= 34) {
                startForeground(NOTIFICATION_ID, notification,
                                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
        } catch (Exception error) {
            if (notificationManager != null) {
                notificationManager.notify(NOTIFICATION_ID, notification);
            }
        }
        return START_NOT_STICKY;
    }

    private void stopForegroundService() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } else {
            stopForeground(true);
        }
        if (notificationManager != null) {
            notificationManager.cancel(NOTIFICATION_ID);
        }
        stopSelf();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && notificationManager != null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Game File Import",
                    NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Progress of game-file copy and extraction into private storage");
            channel.setShowBadge(false);
            notificationManager.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification(long entries, long bytes, String name) {
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        builder.setContentTitle("X-Men Legends II");
        builder.setSmallIcon(android.R.drawable.stat_sys_download);
        builder.setOngoing(true);
        builder.setProgress(0, 0, true);

        String text;
        if (entries > 0) {
            text = entries + (entries == 1 ? " file, " : " files, ") + formatBytes(bytes);
            if (name != null && !name.isEmpty()) {
                text += " \u2022 " + name;
            }
        } else {
            text = "Copying game files into private storage\u2026";
        }
        builder.setContentText(text);

        Intent tapIntent = new Intent(this, XMen2SetupActivity.class);
        tapIntent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, tapIntent, pendingFlags);
        builder.setContentIntent(pendingIntent);

        return builder.build();
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

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
