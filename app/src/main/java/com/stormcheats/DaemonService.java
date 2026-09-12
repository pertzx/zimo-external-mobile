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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class DaemonService extends Service {

    private static final String TAG = "StormDaemon";

    private static final String CHANNEL_ID =
            "storm_daemon_channel";

    private static final String DAEMON_NAME =
            "stormdaemon";

    private static final String CPP_SHARED_NAME =
            "libc++_shared.so";

    private static final String SOCKET_PATH =
            "/data/local/tmp/storm_daemon.sock";

    private static final String PID_PATH =
            "/data/local/tmp/storm_daemon.pid";

    private Process rootProcess;

    private volatile boolean started = false;

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

    private void startRootDaemon() {

        try {

            /*
             * 1. Verificar root
             */

            if (!checkRoot()) {

                Log.e(
                        TAG,
                        "su nao retornou root"
                );

                started = false;

                return;
            }

            /*
             * 2. Parar instancia anterior
             */

            stopOldDaemon();

            /*
             * 3. Definir arquivos locais
             */

            File filesDir =
                    getFilesDir();

            File daemonFile =
                    new File(
                            filesDir,
                            DAEMON_NAME
                    );

            File cppSharedFile =
                    new File(
                            filesDir,
                            CPP_SHARED_NAME
                    );

            /*
             * 4. Escolher ABI
             */

            String abi =
                    chooseAbi();

            Log.i(
                    TAG,
                    "ABI escolhida: " + abi
            );

            /*
             * 5. Extrair executable
             */

            extractDaemon(
                    daemonFile,
                    abi
            );

            /*
             * 6. Extrair libc++ diretamente do APK
             */

            extractCppShared(
                    cppSharedFile,
                    abi
            );

            /*
             * 7. Validacoes
             */

            if (!daemonFile.exists()) {

                throw new IllegalStateException(
                        "stormdaemon nao foi extraido"
                );
            }

            if (!daemonFile.isFile()) {

                throw new IllegalStateException(
                        "stormdaemon nao e um arquivo normal"
                );
            }

            if (daemonFile.length() <= 0) {

                throw new IllegalStateException(
                        "stormdaemon esta vazio"
                );
            }

            if (!cppSharedFile.exists()) {

                throw new IllegalStateException(
                        "libc++_shared.so nao foi extraida"
                );
            }

            if (!cppSharedFile.isFile()) {

                throw new IllegalStateException(
                        "libc++_shared.so nao e um arquivo normal"
                );
            }

            if (cppSharedFile.length() <= 0) {

                throw new IllegalStateException(
                        "libc++_shared.so esta vazia"
                );
            }

            Log.i(
                    TAG,
                    "Daemon: " +
                            daemonFile.getAbsolutePath()
            );

            Log.i(
                    TAG,
                    "Daemon size: " +
                            daemonFile.length()
            );

            Log.i(
                    TAG,
                    "libc++_shared: " +
                            cppSharedFile.getAbsolutePath()
            );

            Log.i(
                    TAG,
                    "libc++_shared size: " +
                            cppSharedFile.length()
            );

            /*
             * 8. Caminhos para shell
             */

            String daemonPath =
                    shellQuote(
                            daemonFile.getAbsolutePath()
                    );

            String cppSharedPath =
                    shellQuote(
                            cppSharedFile.getAbsolutePath()
                    );

            String filesPath =
                    shellQuote(
                            filesDir.getAbsolutePath()
                    );

            /*
             * 9. Comando root
             *
             * O linker vai encontrar libc++_shared.so
             * através do LD_LIBRARY_PATH.
             */

            String command =
                    "chmod 700 " +
                            daemonPath +

                            " && " +

                            "chmod 644 " +
                            cppSharedPath +

                            " && " +

                            "rm -f " +
                            SOCKET_PATH +

                            " && " +

                            "rm -f " +
                            PID_PATH +

                            " && " +

                            "export LD_LIBRARY_PATH=" +
                            filesPath +

                            " && " +

                            "echo '[ROOT] LD_LIBRARY_PATH='\"$LD_LIBRARY_PATH\" " +

                            "&& " +

                            "echo '[ROOT] daemon='\"$0\" " +

                            "&& " +

                            "exec " +
                            daemonPath;

            Log.i(
                    TAG,
                    "LD_LIBRARY_PATH: " +
                            filesDir.getAbsolutePath()
            );

            Log.i(
                    TAG,
                    "Iniciando daemon como root"
            );

            /*
             * 10. Iniciar via su
             */

            rootProcess =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            command
                    )
                            .redirectErrorStream(true)
                            .start();

            /*
             * 11. Ler stdout/stderr
             */

            final Process process =
                    rootProcess;

            new Thread(
                    () -> readProcessOutput(
                            process,
                            "ROOT"
                    ),
                    "StormRootLog"
            ).start();

            /*
             * 12. Monitorar encerramento
             */

            new Thread(
                    () -> waitForRootProcess(
                            process
                    ),
                    "StormRootWait"
            ).start();

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Falha iniciando daemon root",
                    e
            );

            started = false;
        }
    }

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

            int code =
                    process.waitFor();

            Log.i(
                    TAG,
                    "su id: " +
                            output.trim()
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
        File destination,
        String abi
        ) throws Exception {

        String asset =
                "bin/" +
                abi +
                "/" +
                DAEMON_NAME;

        Log.i(
                TAG,
                "Asset escolhido: " +
                        asset
        );

        File temporary =
                new File(
                        getFilesDir(),
                        DAEMON_NAME + ".tmp"
                );

        /*
        * Nunca abrimos o executavel ativo diretamente
        * para escrita. Primeiro criamos um arquivo separado.
        */
        if (temporary.exists()) {
                if (!temporary.delete()) {
                throw new IllegalStateException(
                        "Nao foi possivel remover arquivo temporario: " +
                                temporary.getAbsolutePath()
                );
                }
        }

        try (
                InputStream in =
                        getAssets().open(asset);

                FileOutputStream out =
                        new FileOutputStream(
                                temporary
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

        /*
        * Fecha completamente o arquivo antes de qualquer
        * tentativa de rename.
        */
        if (!temporary.setExecutable(
                true,
                false
        )) {
                Log.w(
                        TAG,
                        "setExecutable falhou no temporario"
                );
        }

        Log.i(
                TAG,
                "Daemon temporario criado: " +
                        temporary.getAbsolutePath()
        );

        /*
        * Primeiro tenta remover o arquivo antigo.
        *
        * Se ele estiver sendo executado, delete pode falhar
        * dependendo do filesystem. Nesse caso tentamos
        * novamente depois de uma pequena espera.
        */
        if (destination.exists()) {

                boolean deleted =
                        destination.delete();

                if (!deleted) {

                Log.w(
                        TAG,
                        "Nao conseguiu remover daemon anterior; " +
                                "tentando parar processo novamente"
                );

                stopOldDaemon();

                Thread.sleep(100);

                deleted =
                        destination.delete();
                }

                if (!deleted) {

                /*
                * Nao sobrescrevemos um ELF que ainda possa
                * estar em uso.
                */
                temporary.delete();

                throw new IllegalStateException(
                        "Nao foi possivel substituir stormdaemon antigo"
                );
                }
        }

        /*
        * renameTo move o arquivo pronto para o nome final.
        */
        if (!temporary.renameTo(destination)) {

                temporary.delete();

                throw new IllegalStateException(
                        "Falha movendo daemon temporario para: " +
                                destination.getAbsolutePath()
                );
        }

        /*
        * Garantir permissao depois do rename.
        */
        if (!destination.setExecutable(
                true,
                false
        )) {
                Log.w(
                        TAG,
                        "setExecutable falhou"
                );
        }

        Log.i(
                TAG,
                "stormdaemon extraido: " +
                        destination.getAbsolutePath()
        );

        Log.i(
                TAG,
                "stormdaemon size: " +
                        destination.length()
        );
        }

    /**
     * Extrai libc++_shared.so diretamente do APK.
     *
     * Isso evita depender de:
     *
     * getApplicationInfo().nativeLibraryDir
     *
     * porque esse caminho pode nao conter fisicamente
     * a biblioteca nesta configuracao do Android/Gradle.
     */
    private void extractCppShared(
            File destination,
            String abi
    ) throws Exception {

        String apkEntry =
                "lib/" +
                abi +
                "/" +
                CPP_SHARED_NAME;

        Log.i(
                TAG,
                "Procurando biblioteca no APK: " +
                        apkEntry
        );

        String apkPath =
                getApplicationInfo().sourceDir;

        Log.i(
                TAG,
                "APK: " +
                        apkPath
        );

        boolean found = false;

        try (
                ZipFile zip =
                        new ZipFile(apkPath)
        ) {

            ZipEntry entry =
                    zip.getEntry(apkEntry);

            if (entry == null) {

                /*
                 * Algumas construcoes podem usar a outra
                 * nomenclatura de ABI. Tentamos localizar
                 * qualquer entrada correspondente.
                 */

                Log.w(
                        TAG,
                        "Entrada exata nao encontrada. Procurando por nome..."
                );

                for (
                        java.util.Enumeration<? extends ZipEntry> entries =
                                zip.entries();
                        entries.hasMoreElements();
                ) {

                    ZipEntry candidate =
                            entries.nextElement();

                    String name =
                            candidate.getName();

                    if (
                            name.endsWith(
                                    "/" +
                                            CPP_SHARED_NAME
                            )
                    ) {

                        Log.i(
                                TAG,
                                "Encontrada entrada alternativa: " +
                                        name
                        );

                        entry =
                                candidate;

                        found = true;

                        break;
                    }
                }

            } else {

                found = true;
            }

            if (!found || entry == null) {

                throw new IllegalStateException(
                        "Entrada " +
                                CPP_SHARED_NAME +
                                " nao encontrada no APK"
                );
            }

            if (entry.isDirectory()) {

                throw new IllegalStateException(
                        "Entrada da libc++ e um diretorio"
                );
            }

            Log.i(
                    TAG,
                    "Extraindo entrada APK: " +
                            entry.getName()
            );

            try (
                    InputStream in =
                            zip.getInputStream(entry);

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
        }

        Log.i(
                TAG,
                "libc++_shared.so extraida: " +
                        destination.getAbsolutePath()
        );
    }

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
                "ABI ARM nao suportada"
        );
    }

    private void stopOldDaemon() {
    try {

        String daemonPath =
                shellQuote(
                        new File(
                                getFilesDir(),
                                DAEMON_NAME
                        ).getAbsolutePath()
                );

        String command =
                "if [ -f " +
                        PID_PATH +
                        " ]; then " +

                        "kill -9 $(cat " +
                        PID_PATH +
                        ") 2>/dev/null; " +

                        "fi; " +

                        "pkill -9 -f " +
                        daemonPath +
                        " 2>/dev/null; " +

                        "rm -f " +
                        PID_PATH +
                        "; " +

                        "rm -f " +
                        SOCKET_PATH;

        Process process =
                new ProcessBuilder(
                        "su",
                        "-c",
                        command
                )
                        .redirectErrorStream(true)
                        .start();

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
                    "if [ -f " +
                            PID_PATH +
                            " ]; then " +

                            "kill -9 $(cat " +
                            PID_PATH +
                            ") " +
                            "2>/dev/null; " +

                            "fi; " +

                            "rm -f " +
                            PID_PATH +
                            "; " +

                            "rm -f " +
                            SOCKET_PATH;

            Process process =
                    new ProcessBuilder(
                            "su",
                            "-c",
                            command
                    )
                            .redirectErrorStream(true)
                            .start();

            process.waitFor();

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
                    (n =
                            process
                                    .getInputStream()
                                    .read(buffer)) > 0
            ) {

                String text =
                        new String(
                                buffer,
                                0,
                                n
                        );

                String[] lines =
                        text.split(
                                "\\r?\\n"
                        );

                for (
                        String line :
                        lines
                ) {

                    if (
                            !line.isEmpty()
                    ) {

                        Log.i(
                                TAG,
                                "[" +
                                        tag +
                                        "] " +
                                        line
                        );
                    }
                }
            }

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Erro lendo stdout do daemon",
                    e
            );
        }
    }

    private void waitForRootProcess(
            Process process
    ) {

        try {

            int exitCode =
                    process.waitFor();

            Log.e(
                    TAG,
                    "[ROOT] processo terminou. exitCode=" +
                            exitCode
            );

            if (
                    rootProcess == process
            ) {

                rootProcess = null;
            }

            started = false;

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Erro aguardando daemon root",
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
    public IBinder onBind(
            Intent intent
    ) {
        return null;
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