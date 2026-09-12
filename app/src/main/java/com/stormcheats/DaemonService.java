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
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class DaemonService extends Service {

    private static final String TAG = "StormDaemon";

    private static final String CHANNEL_ID =
            "storm_daemon_channel";

    private static final String DAEMON_LIBRARY =
            "libdaemon.so";

    private static final String CPP_SHARED_LIBRARY =
            "libc++_shared.so";

    private static final String SOCKET_PATH =
            "/data/local/tmp/storm_daemon.sock";

    private static final String PID_PATH =
            "/data/local/tmp/storm_daemon.pid";

    private volatile boolean started = false;

    private Process rootProcess;

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
        Log.i(
                TAG,
                "DaemonService iniciado"
        );

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

    @Override
    public void onDestroy() {
        Log.i(
                TAG,
                "DaemonService destruindo"
        );

        stopRootDaemon();

        started = false;

        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private Notification buildNotification() {
        return new Notification.Builder(
                this,
                CHANNEL_ID
        )
                .setContentTitle(
                        "Storm Daemon"
                )
                .setContentText(
                        "Backend root em execucao"
                )
                .setSmallIcon(
                        android.R.drawable.ic_menu_info_details
                )
                .setOngoing(true)
                .build();
    }

    // ================================================================
    // ROOT DAEMON
    // ================================================================

    private void startRootDaemon() {
        try {
            if (!checkRoot()) {
                Log.e(
                        TAG,
                        "su nao retornou root"
                );

                started = false;
                return;
            }

            stopRootDaemon();

            File filesDir =
                    getFilesDir();

            File daemonLibrary =
                    new File(
                            filesDir,
                            DAEMON_LIBRARY
                    );

            File cppSharedLibrary =
                    new File(
                            filesDir,
                            CPP_SHARED_LIBRARY
                    );

            String abi =
                    chooseAbi();

            Log.i(
                    TAG,
                    "ABI do daemon: " + abi
            );

            extractNativeLibrary(
                    daemonLibrary,
                    abi,
                    DAEMON_LIBRARY
            );

            extractNativeLibrary(
                    cppSharedLibrary,
                    abi,
                    CPP_SHARED_LIBRARY
            );

            validateFile(
                    daemonLibrary,
                    DAEMON_LIBRARY
            );

            validateFile(
                    cppSharedLibrary,
                    CPP_SHARED_LIBRARY
            );

            String apkPath =
                    getApplicationInfo().sourceDir;

            String classPath =
                    shellQuote(apkPath);

            String daemonPath =
                    shellQuote(
                            daemonLibrary
                                    .getAbsolutePath()
                    );

            String cppPath =
                    shellQuote(
                            filesDir
                                    .getAbsolutePath()
                    );

            String pidPath =
                    shellQuote(PID_PATH);

            String appProcess =
                    chooseAppProcess(abi);

            String command =
                    "rm -f " +
                    shellQuote(SOCKET_PATH) +

                    " && " +

                    "rm -f " +
                    shellQuote(PID_PATH) +

                    " && " +

                    "chmod 755 " +
                    daemonPath +

                    " && " +

                    "chmod 644 " +
                    shellQuote(
                            cppSharedLibrary
                                    .getAbsolutePath()
                    ) +

                    " && " +

                    "export LD_LIBRARY_PATH=" +
                    cppPath +

                    " && " +

                    "export CLASSPATH=" +
                    classPath +

                    " && " +

                    "exec " +
                    appProcess +
                    " /system/bin " +
                    "com.stormcheats.RootDaemonMain " +
                    daemonPath +
                    " " +
                    pidPath;

            Log.i(
                    TAG,
                    "APK: " + apkPath
            );

            Log.i(
                    TAG,
                    "daemon .so: " +
                            daemonLibrary
                                    .getAbsolutePath()
            );

            Log.i(
                    TAG,
                    "libc++: " +
                            cppSharedLibrary
                                    .getAbsolutePath()
            );

            Log.i(
                    TAG,
                    "app_process: " +
                            appProcess
            );

            Log.i(
                    TAG,
                    "Iniciando libdaemon como root"
            );

            rootProcess =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            command
                    )
                            .redirectErrorStream(true)
                            .start();

            final Process process =
                    rootProcess;

            new Thread(
                    () -> readProcessOutput(
                            process,
                            "ROOT"
                    ),
                    "StormRootLog"
            ).start();

            new Thread(
                    () -> waitForRootProcess(
                            process
                    ),
                    "StormRootWait"
            ).start();

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Falha iniciando libdaemon root",
                    e
            );

            started = false;
        }
    }

    // ================================================================
    // ROOT CHECK
    // ================================================================

    private boolean checkRoot() {
        try {
            Process process =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            "id"
                    )
                            .redirectErrorStream(true)
                            .start();

            byte[] buffer =
                    new byte[1024];

            int n =
                    process
                            .getInputStream()
                            .read(buffer);

            String output =
                    n > 0
                            ? new String(
                                    buffer,
                                    0,
                                    n
                            )
                            : "";

            int exitCode =
                    process.waitFor();

            Log.i(
                    TAG,
                    "su id: " +
                            output.trim()
            );

            return exitCode == 0 &&
                    output.contains("uid=0");

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Falha verificando root",
                    e
            );

            return false;
        }
    }

    // ================================================================
    // ABI
    // ================================================================

    private String chooseAbi() {

        for (
                String abi :
                Build.SUPPORTED_ABIS
        ) {

            if (
                    "arm64-v8a".equals(
                            abi
                    )
            ) {
                return "arm64-v8a";
            }

            if (
                    "armeabi-v7a".equals(
                            abi
                    )
            ) {
                return "armeabi-v7a";
            }
        }

        throw new IllegalStateException(
                "Nenhuma ABI ARM suportada"
        );
    }

    private String chooseAppProcess(
            String abi
    ) {

        if (
                "arm64-v8a".equals(
                        abi
                )
        ) {

            File appProcess64 =
                    new File(
                            "/system/bin/app_process64"
                    );

            if (
                    appProcess64.exists()
            ) {
                return "/system/bin/app_process64";
            }
        }

        File appProcess32 =
                new File(
                        "/system/bin/app_process32"
                );

        if (
                appProcess32.exists()
        ) {
            return "/system/bin/app_process32";
        }

        return "/system/bin/app_process";
    }

    // ================================================================
    // EXTRAÇÃO DAS .SO
    // ================================================================

    private void extractNativeLibrary(
            File destination,
            String abi,
            String libraryName
    ) throws Exception {

        String exactEntry =
                "lib/" +
                abi +
                "/" +
                libraryName;

        Log.i(
                TAG,
                "Procurando no APK: " +
                        exactEntry
        );

        String apkPath =
                getApplicationInfo().sourceDir;

        try (
                ZipFile zip =
                        new ZipFile(apkPath)
        ) {

            ZipEntry entry =
                    zip.getEntry(
                            exactEntry
                    );

            if (
                    entry == null
            ) {

                Log.w(
                        TAG,
                        "Entrada exata nao encontrada: " +
                                exactEntry
                );

                Enumeration<? extends ZipEntry>
                        entries =
                        zip.entries();

                while (
                        entries.hasMoreElements()
                ) {

                    ZipEntry candidate =
                            entries.nextElement();

                    String name =
                            candidate.getName();

                    if (
                            name.endsWith(
                                    "/" +
                                            libraryName
                            )
                    ) {

                        entry =
                                candidate;

                        Log.i(
                                TAG,
                                "Entrada encontrada: " +
                                        name
                        );

                        break;
                    }
                }
            }

            if (
                    entry == null
            ) {

                throw new IllegalStateException(
                        libraryName +
                                " nao encontrado no APK"
                );
            }

            File temporary =
                    new File(
                            getFilesDir(),
                            libraryName +
                                    ".tmp"
                    );

            if (
                    temporary.exists()
            ) {
                temporary.delete();
            }

            try (
                    InputStream in =
                            zip.getInputStream(
                                    entry
                            );

                    FileOutputStream out =
                            new FileOutputStream(
                                    temporary
                            )
            ) {

                byte[] buffer =
                        new byte[
                                16 * 1024
                        ];

                int n;

                while (
                        (n = in.read(buffer)) >
                                0
                ) {

                    out.write(
                            buffer,
                            0,
                            n
                    );
                }

                out.flush();
            }

            if (
                    destination.exists()
            ) {
                destination.delete();
            }

            if (
                    !temporary.renameTo(
                            destination
                    )
            ) {

                temporary.delete();

                throw new IllegalStateException(
                        "Falha movendo " +
                                libraryName
                );
            }

            if (
                    !destination.setReadable(
                            true,
                            false
                    )
            ) {
                Log.w(
                        TAG,
                        "setReadable falhou: " +
                                libraryName
                );
            }
        }

        Log.i(
                TAG,
                "Extraido: " +
                        destination
                                .getAbsolutePath() +
                        " (" +
                        destination.length() +
                        " bytes)"
        );
    }

    private void validateFile(
            File file,
            String name
    ) {

        if (
                !file.exists()
        ) {
            throw new IllegalStateException(
                    name +
                            " nao existe"
            );
        }

        if (
                !file.isFile()
        ) {
            throw new IllegalStateException(
                    name +
                            " nao e arquivo"
            );
        }

        if (
                file.length() <= 0
        ) {
            throw new IllegalStateException(
                    name +
                            " esta vazio"
            );
        }
    }

    // ================================================================
    // PARAR DAEMON
    // ================================================================

    private void stopRootDaemon() {

        try {

            Process process =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            "if [ -f " +
                                    shellQuote(
                                            PID_PATH
                                    ) +
                                    " ]; then " +
                                    "kill -TERM $(cat " +
                                    shellQuote(
                                            PID_PATH
                                    ) +
                                    ") 2>/dev/null; " +
                                    "sleep 0.2; " +
                                    "kill -KILL $(cat " +
                                    shellQuote(
                                            PID_PATH
                                    ) +
                                    ") 2>/dev/null; " +
                                    "fi; " +
                                    "rm -f " +
                                    shellQuote(
                                            PID_PATH
                                    ) +
                                    "; rm -f " +
                                    shellQuote(
                                            SOCKET_PATH
                                    )
                    )
                            .redirectErrorStream(true)
                            .start();

            process.waitFor();

        } catch (Throwable e) {

            Log.w(
                    TAG,
                    "Falha parando daemon anterior: " +
                            e.getMessage()
            );
        }

        if (
                rootProcess != null
        ) {

            try {
                rootProcess.destroy();
            } catch (Throwable ignored) {
            }

            rootProcess = null;
        }
    }

    // ================================================================
    // LOG DO ROOT
    // ================================================================

    private void readProcessOutput(
            Process process,
            String prefix
    ) {

        try {

            InputStream input =
                    process.getInputStream();

            byte[] buffer =
                    new byte[4096];

            int n;

            StringBuilder pending =
                    new StringBuilder();

            while (
                    (n = input.read(buffer))
                            > 0
            ) {

                pending.append(
                        new String(
                                buffer,
                                0,
                                n
                        )
                );

                int newline;

                while (
                        (newline =
                                pending.indexOf("\n"))
                                >= 0
                ) {

                    String line =
                            pending
                                    .substring(
                                            0,
                                            newline
                                    )
                                    .trim();

                    pending.delete(
                            0,
                            newline + 1
                    );

                    if (!line.isEmpty()) {

                        Log.i(
                                TAG,
                                "[" +
                                        prefix +
                                        "] " +
                                        line
                        );
                    }
                }
            }

        } catch (Throwable e) {

            Log.w(
                    TAG,
                    "Falha lendo log root: " +
                            e.getMessage()
            );
        }
    }

    // ================================================================
    // MONITOR
    // ================================================================

    private void waitForRootProcess(
            Process process
    ) {

        try {

            int code =
                    process.waitFor();

            Log.e(
                    TAG,
                    "RootDaemon terminou. code=" +
                            code
            );

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Erro aguardando root daemon",
                    e
            );

        } finally {

            if (
                    rootProcess ==
                            process
            ) {
                rootProcess = null;
            }

            started = false;
        }
    }

    // ================================================================
    // UTIL
    // ================================================================

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

    private void createNotificationChannel() {

        if (
                Build.VERSION.SDK_INT >=
                        Build.VERSION_CODES.O
        ) {

            NotificationChannel channel =
                    new NotificationChannel(
                            CHANNEL_ID,
                            "Storm Daemon",
                            NotificationManager
                                    .IMPORTANCE_LOW
                    );

            NotificationManager manager =
                    getSystemService(
                            NotificationManager.class
                    );

            if (
                    manager != null
            ) {
                manager.createNotificationChannel(
                        channel
                );
            }
        }
    }
}