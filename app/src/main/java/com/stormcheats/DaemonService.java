package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

public class DaemonService extends Service {
    private static final String TAG = "StormDaemon";
    private static final String CHANNEL_ID = "storm_daemon_channel";
    private Thread nativeThread;

    static {
        try {
            System.loadLibrary("daemon");
            Log.i(TAG, "libdaemon.so carregada");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Falha ao carregar libdaemon.so: " + e.getMessage());
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "DaemonService iniciado");

        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Storm Daemon")
                .setContentText("Serviço de backend em execução")
                .setSmallIcon(android.R.drawable.ic_menu_info_details)
                .setOngoing(true)
                .build();

        startForeground(2, notification);

        nativeThread = new Thread(() -> {
            try {
                nativeRunDaemon();
            } catch (Exception e) {
                Log.e(TAG, "Erro no daemon nativo: " + e.getMessage());
            }
        });
        nativeThread.start();

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        nativeStopDaemon();
        if (nativeThread != null) {
            nativeThread.interrupt();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Storm Daemon",
                    NotificationManager.IMPORTANCE_LOW
            );
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private native void nativeRunDaemon();
    private native void nativeStopDaemon();
}