package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class DaemonService extends Service {

    private static final String TAG = "StormDaemon";
    private static final String CHANNEL_ID = "storm_daemon_channel";

    private Process rootProcess;
    private boolean started = false;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(
            Intent intent,
            int flags,
            int startId
    ) {
        Log.i(TAG, "DaemonService iniciado");

        startForeground(
                2,
                buildNotification()
        );

        if (!started) {
            started = true;

            new Thread(
                    this::startRootDaemon,
                    "StormRootLauncher"
            ).start();
        }

        return START_STICKY;
    }

    private Notification buildNotification() {
        return new Notification.Builder(
                this,
                CHANNEL_ID
        )
                .setContentTitle("Storm Daemon")
                .setContentText("Backend root em execucao")
                .setSmallIcon(
                        android.R.drawable.ic_menu_info_details
                )
                .setOngoing(true)
                .build();
    }

    private void startRootDaemon() {
        try {
            if (!checkRoot()) {
                Log.e(
                        TAG,
                        "su nao retornou root"
                );
                return;
            }

            stopOldDaemon();

            File daemonFile =
                    new File(
                            getFilesDir(),
                            "stormdaemon"
                    );

            extractDaemon(daemonFile);

            String quotedBinary =
                    shellQuote(
                            daemonFile.getAbsolutePath()
                    );

            String command =
                    "chmod 700 " + quotedBinary +
                    " && " +
                    "rm -f /data/local/tmp/storm_daemon.sock" +
                    " && " +
                    "rm -f /data/local/tmp/storm_daemon.pid" +
                    " && " +
                    "exec " + quotedBinary;

            Log.i(
                    TAG,
                    "Iniciando daemon como root"
            );

            rootProcess = new ProcessBuilder(
                    "su",
                    "-c",
                    command
            )
                    .redirectErrorStream(true)
                    .start();

            new Thread(
                    () -> readProcessOutput(
                            rootProcess,
                            "ROOT"
                    ),
                    "StormRootLog"
            ).start();

        } catch (Throwable e) {
            Log.e(
                    TAG,
                    "Falha iniciando daemon root",
                    e
            );
        }
    }

    private boolean checkRoot() {
        try {
            Process process =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            "id"
                    ).redirectErrorStream(true).start();

            byte[] buffer =
                    new byte[1024];

            int n =
                    process.getInputStream().read(
                            buffer
                    );

            String output =
                    n > 0
                            ? new String(
                                    buffer,
                                    0,
                                    n
                            )
                            : "";

            int code =
                    process.waitFor();

            Log.i(
                    TAG,
                    "su id: " + output
            );

            return code == 0 &&
                    output.contains("uid=0");
        } catch (Throwable e) {
            Log.e(
                    TAG,
                    "Falha testando root",
                    e
            );

            return false;
        }
    }

    private void extractDaemon(
            File destination
    ) throws Exception {

        String abi = chooseAbi();

        String asset =
                "bin/" +
                abi +
                "/stormdaemon";

        Log.i(
                TAG,
                "Asset escolhido: " + asset
        );

        try (
                InputStream in =
                        getAssets().open(asset);

                FileOutputStream out =
                        new FileOutputStream(
                                destination
                        )
        ) {
            byte[] buffer =
                    new byte[16 * 1024];

            int n;

            while (
                    (n = in.read(buffer)) > 0
            ) {
                out.write(
                        buffer,
                        0,
                        n
                );
            }

            out.flush();
        }

        if (!destination.setExecutable(
                true,
                false
        )) {
            Log.w(
                    TAG,
                    "setExecutable falhou"
            );
        }
    }

    private String chooseAbi() {

        for (String abi :
                android.os.Build.SUPPORTED_ABIS) {

            if ("armeabi-v7a".equals(abi))
                return "armeabi-v7a";

            if ("arm64-v8a".equals(abi))
                return "arm64-v8a";
        }

        throw new IllegalStateException(
                "ABI ARM nao suportada"
        );
    }

    private void stopOldDaemon() {
        try {
            String command =
                    "if [ -f /data/local/tmp/storm_daemon.pid ]; then " +
                    "kill -9 $(cat /data/local/tmp/storm_daemon.pid) 2>/dev/null; " +
                    "fi; " +
                    "rm -f /data/local/tmp/storm_daemon.pid; " +
                    "rm -f /data/local/tmp/storm_daemon.sock";

            Process process =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            command
                    ).start();

            process.waitFor();

        } catch (Throwable e) {
            Log.w(
                    TAG,
                    "Falha limpando daemon antigo",
                    e
            );
        }
    }

    private void stopRootDaemon() {
        try {
            String command =
                    "if [ -f /data/local/tmp/storm_daemon.pid ]; then " +
                    "kill -9 $(cat /data/local/tmp/storm_daemon.pid) 2>/dev/null; " +
                    "fi; " +
                    "rm -f /data/local/tmp/storm_daemon.pid; " +
                    "rm -f /data/local/tmp/storm_daemon.sock";

            new ProcessBuilder(
                    "su",
                    "-c",
                    command
            ).start();

        } catch (Throwable e) {
            Log.e(
                    TAG,
                    "Falha parando daemon",
                    e
            );
        }
    }

    private void readProcessOutput(
            Process process,
            String tag
    ) {
        try {
            byte[] buffer =
                    new byte[4096];

            int n;

            while (
                    (n = process.getInputStream().read(buffer)) > 0
            ) {
                String text =
                        new String(
                                buffer,
                                0,
                                n
                        );

                Log.i(
                        TAG,
                        "[" + tag + "] " + text
                );
            }
        } catch (Throwable e) {
            Log.e(
                    TAG,
                    "Erro lendo stdout do daemon",
                    e
            );
        }
    }

    private static String shellQuote(
            String value
    ) {
        return "'" +
                value.replace(
                        "'",
                        "'\\''"
                ) +
                "'";
    }

    @Override
    public void onDestroy() {
        stopRootDaemon();

        if (rootProcess != null) {
            rootProcess.destroy();
            rootProcess = null;
        }

        started = false;

        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {

        if (Build.VERSION.SDK_INT >=
                Build.VERSION_CODES.O) {

            NotificationChannel channel =
                    new NotificationChannel(
                            CHANNEL_ID,
                            "Storm Daemon",
                            NotificationManager.IMPORTANCE_LOW
                    );

            NotificationManager manager =
                    getSystemService(
                            NotificationManager.class
                    );

            if (manager != null) {
                manager.createNotificationChannel(
                        channel
                );
            }
        }
    }
}