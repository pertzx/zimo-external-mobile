package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * ============================================================================
 * DaemonService - GERENCIADOR DO DAEMON-PONTE (root) — STEALTH
 * ============================================================================
 *
 * STEALTH (anti-ban):
 *  - Nada de nomes fixos conhecidos ("stormdaemon", "stormbridge.sock",
 *    "stormbridge.log"). Tudo vive num diretorio oculto com token RANDOMICO
 *    por instalacao: /data/local/tmp/.sysa_<tok>/  (o ponto na frente esconde
 *    de ls sem -a; o token randomico impede lista de nomes conhecidos).
 *      .sysa_<tok>/d                  -> binario do daemon
 *      .sysa_<tok>/libc++_shared.so   -> lib (nome real, dentro do dir oculto)
 *      .sysa_<tok>/s                  -> socket AF_UNIX
 *      .sysa_<tok>/p                  -> pidfile
 *  - O path do socket e publicado pro client (libclient.so) em ARQUIVO
 *    PRIVADO do app: files/stormbridge.path (invisivel pro jogo).
 *  - O daemon NAO recebe --log (nunca cria arquivo de log em disco) e
 *    NAO recebe --verbose (nunca escreve no logcat).
 *  - O daemon se disfarcA de "appstats" (comm/cmdline) la dentro dele.
 *  - Na primeira execucao desta versao, os rastros da versao antiga
 *    (stormdaemon, stormbridge.*, libc++_shared.so solto) sao apagados.
 * ============================================================================
 */
public class DaemonService extends Service {

    private static final String TAG = "StormDaemonMgr";

    private static final String CHANNEL_ID = "storm_daemon_channel";

    // ------------------------------------------------------------------
    // Base fixa (publica) + diretorio oculto com token randomico
    // ------------------------------------------------------------------

    private static final String REMOTE_BASE = "/data/local/tmp";

    // Preenchidos em initPaths() com o token da instalacao
    private String remoteDir;      // /data/local/tmp/.sysa_<tok>
    private String remoteDaemon;   // <remoteDir>/d
    private String remoteCppShared;// <remoteDir>/libc++_shared.so
    private String bridgeSocket;   // <remoteDir>/s
    private String bridgePidfile;  // <remoteDir>/p

    private static final String PATH_FILE_NAME = "stormbridge.path";

    private static final String PREFS = "sys_prefs";
    private static final String PREF_TOKEN = "sys_token";

    private static final int BRIDGE_MAGIC = 0x53544F52; // "STOR"

    private static final int BRIDGE_PROTO_VERSION = 1;

    private static final int BRIDGE_CMD_PING = 1;

    // ------------------------------------------------------------------

    private final AtomicBoolean installing = new AtomicBoolean(false);

    private volatile boolean started = false;

    private volatile Process rootProcess;

    private Thread watchdogThread;

    // Backoff do watchdog: 3s, 5s, 10s, 15s, 20s, 20s...
    private long restartBackoffMs = 3000;

    // ------------------------------------------------------------------
    // CICLO DE VIDA DO SERVIÇO
    // ------------------------------------------------------------------

    @Override
    public void onCreate() {
        super.onCreate();

        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startForeground(2, buildNotification());

        if (installing.compareAndSet(false, true)) {
            new Thread(this::installAndStart, "StormDaemonInstaller").start();
        }

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopWatchdog();
        stopStormDaemon();

        installing.set(false);
        started = false;

        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private Notification buildNotification() {
        return new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Serviço do sistema")
                .setContentText("Manutenção ativa")
                .setSmallIcon(android.R.drawable.ic_menu_manage)
                .setOngoing(true)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Manutenção",
                    NotificationManager.IMPORTANCE_LOW);
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }

    // ==================================================================
    // TOKEN RANDOMICO PERSISTENTE (identidade oculta da instalacao)
    // ==================================================================

    private String getToken() {
        SharedPreferences sp = getSharedPreferences(PREFS, MODE_PRIVATE);
        String tok = sp.getString(PREF_TOKEN, null);

        if (tok == null || tok.length() < 8) {
            StringBuilder sb = new StringBuilder();
            Random rnd = new Random();
            String alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";

            for (int i = 0; i < 12; i++) {
                sb.append(alphabet.charAt(rnd.nextInt(alphabet.length())));
            }

            tok = sb.toString();
            sp.edit().putString(PREF_TOKEN, tok).apply();
        }

        return tok;
    }

    private void initPaths() {
        String tok = getToken();

        remoteDir = REMOTE_BASE + "/.sysa_" + tok;
        remoteDaemon = remoteDir + "/d";
        remoteCppShared = remoteDir + "/libc++_shared.so";
        bridgeSocket = remoteDir + "/s";
        bridgePidfile = remoteDir + "/p";

        // Publica o path do socket pro client (libclient.so le daqui).
        // Arquivo PRIVADO do app — o jogo nao consegue ler.
        try (FileOutputStream fo = openFileOutput(PATH_FILE_NAME, MODE_PRIVATE)) {
            fo.write(bridgeSocket.getBytes("UTF-8"));
            fo.flush();
        } catch (Throwable ignored) {
        }
    }

    // ==================================================================
    // 1) INSTALAÇÃO + INICIALIZAÇÃO
    // ==================================================================

    private void installAndStart() {
        try {
            initPaths();

            if (!checkRoot()) {
                installing.set(false);
                return;
            }

            // Mata qualquer daemon anterior (novo e LEGADO da versao antiga)
            // e apaga os rastros com nome fixo que a versao antiga deixou.
            stopStormDaemon();

            String abi = chooseAbi();

            File localDaemon = extractAsset("bin/" + abi + "/stormdaemon", "stormdaemon");
            validateFile(localDaemon, "stormdaemon (" + abi + ")");

            File localCppShared = extractNativeLibrary(abi, "libc++_shared.so");
            validateFile(localCppShared, "libc++_shared.so");

            if (!installFiles(localDaemon, localCppShared)) {
                installing.set(false);
                return;
            }

            startStormDaemon();
            startWatchdog();

            started = true;
            installing.set(false);

        } catch (Throwable e) {
            installing.set(false);
        }
    }

    // ==================================================================
    // 2) ROOT
    // ==================================================================

    private boolean checkRoot() {
        try {
            Process process = new ProcessBuilder("su", "-c", "id")
                    .redirectErrorStream(true)
                    .start();

            byte[] buffer = new byte[1024];
            int n = process.getInputStream().read(buffer);
            String output = n > 0 ? new String(buffer, 0, n).trim() : "";
            int exitCode = process.waitFor();

            return exitCode == 0 && output.contains("uid=0");

        } catch (Throwable e) {
            return false;
        }
    }

    private String runAsRoot(String command) {
        try {
            Process process = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();

            byte[] buffer = new byte[4096];
            StringBuilder out = new StringBuilder();
            InputStream in = process.getInputStream();

            int n;
            while ((n = in.read(buffer)) > 0) {
                out.append(new String(buffer, 0, n));
            }

            process.waitFor();
            return out.toString().trim();

        } catch (Throwable e) {
            return "";
        }
    }

    // ==================================================================
    // 3) ABI + EXTRAÇÃO DO APK
    // ==================================================================

    private String chooseAbi() {
        for (String abi : Build.SUPPORTED_ABIS) {
            if ("arm64-v8a".equals(abi)) return "arm64-v8a";
            if ("armeabi-v7a".equals(abi)) return "armeabi-v7a";
        }

        throw new IllegalStateException("Nenhuma ABI ARM suportada");
    }

    /**
     * Extrai um asset para filesDir (área do app) e devolve o File local.
     * A cópia para o diretorio oculto é feita depois VIA ROOT.
     */
    private File extractAsset(String assetPath, String name) throws Exception {
        File destination = new File(getFilesDir(), name + ".tmp");
        File finalFile = new File(getFilesDir(), name);

        try (InputStream in = getAssets().open(assetPath);
             FileOutputStream out = new FileOutputStream(destination, false)) {

            byte[] buffer = new byte[16 * 1024];
            int n;

            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
            }

            out.flush();
        }

        if (finalFile.exists()) {
            //noinspection ResultOfMethodCallIgnored
            finalFile.delete();
        }

        if (!destination.renameTo(finalFile)) {
            //noinspection ResultOfMethodCallIgnored
            destination.delete();
            throw new IllegalStateException("Falha renomeando " + name);
        }

        return finalFile;
    }

    /**
     * Extrai uma .so do APK (lib/<abi>/<name>) para filesDir.
     */
    private File extractNativeLibrary(String abi, String libraryName) throws Exception {
        String apkPath = getApplicationInfo().sourceDir;
        String entryName = "lib/" + abi + "/" + libraryName;

        File finalFile = new File(getFilesDir(), libraryName);
        File temporary = new File(getFilesDir(), libraryName + ".tmp");

        try (java.util.zip.ZipFile zip = new java.util.zip.ZipFile(apkPath)) {
            java.util.zip.ZipEntry entry = zip.getEntry(entryName);

            if (entry == null) {
                throw new IllegalStateException(libraryName + " não encontrado no APK (" + entryName + ")");
            }

            try (InputStream in = zip.getInputStream(entry);
                 FileOutputStream out = new FileOutputStream(temporary, false)) {

                byte[] buffer = new byte[16 * 1024];
                int n;

                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                }

                out.flush();
            }
        }

        if (finalFile.exists()) {
            //noinspection ResultOfMethodCallIgnored
            finalFile.delete();
        }

        if (!temporary.renameTo(finalFile)) {
            //noinspection ResultOfMethodCallIgnored
            temporary.delete();
            throw new IllegalStateException("Falha renomeando " + libraryName);
        }

        return finalFile;
    }

    private void validateFile(File file, String name) {
        if (!file.exists() || !file.isFile()) {
            throw new IllegalStateException(name + " não existe/ não é arquivo");
        }

        if (file.length() <= 0) {
            throw new IllegalStateException(name + " está vazio");
        }
    }

    // ==================================================================
    // 4) INSTALAÇÃO NO DIRETORIO OCULTO (via su)
    // ==================================================================

    private boolean installFiles(File localDaemon, File localCppShared) {
        String qDir = shellQuote(remoteDir);
        String qLocalDaemon = shellQuote(localDaemon.getAbsolutePath());
        String qLocalCpp = shellQuote(localCppShared.getAbsolutePath());
        String qDaemon = shellQuote(remoteDaemon);
        String qCpp = shellQuote(remoteCppShared);

        String command =
                "mkdir -p " + qDir +
                " && cat " + qLocalDaemon + " > " + qDaemon +
                " && chmod 755 " + qDaemon +
                " && chown root:root " + qDaemon +
                " && cat " + qLocalCpp + " > " + qCpp +
                " && chmod 644 " + qCpp +
                // 0711: outros PODEM atravessar (conectar no socket) mas
                // NAO podem listar o conteudo (r negado pra others).
                " && chmod 0711 " + qDir +
                " && chmod 0711 " + shellQuote(REMOTE_BASE);

        runAsRoot(command);

        // Confirma tamanho instalado = tamanho extraído
        try {
            long expected = localDaemon.length();
            String check = runAsRoot("stat -c %s " + qDaemon);

            if (!check.isEmpty()) {
                long installed = Long.parseLong(check.trim());
                if (installed != expected) {
                    return false;
                }
            }
        } catch (Throwable ignored) {
        }

        return true;
    }

    // ==================================================================
    // 5) INICIAR O DAEMON COMO ROOT (mudo: sem --log, sem --verbose)
    // ==================================================================

    private void startStormDaemon() {
        try {
            String command =
                    "export LD_LIBRARY_PATH=" + shellQuote(remoteDir) +
                    " && exec " + shellQuote(remoteDaemon) +
                    " --socket " + shellQuote(bridgeSocket) +
                    " --pidfile " + shellQuote(bridgePidfile);

            rootProcess = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();

            final Process process = rootProcess;

            // Consumo a saida sem logar (o daemon e mudo; so drenamos o pipe)
            new Thread(() -> drainProcessOutput(process), "StormDaemonLog").start();

            // Aguarda o socket responder (até 5s)
            for (int i = 0; i < 25; i++) {
                if (pingBridge()) {
                    return;
                }

                Thread.sleep(200);
            }

        } catch (Throwable ignored) {
        }
    }

    // ==================================================================
    // 6) WATCHDOG (monitorar + reiniciar + checar saúde da ponte)
    // ==================================================================

    private void startWatchdog() {
        stopWatchdog();

        watchdogThread = new Thread(() -> {
            int pingCounter = 0;

            while (!Thread.currentThread().isInterrupted()) {
                try {
                    Thread.sleep(3000);

                    Process process = rootProcess;

                    if (process == null) {
                        onDaemonDead("processo nulo");
                        continue;
                    }

                    try {
                        process.exitValue();
                        onDaemonDead("exit");
                        continue;

                    } catch (IllegalThreadStateException alive) {
                        // Ainda rodando - ok
                    }

                    // PING de saúde a cada 5 ciclos (15s)
                    if (++pingCounter >= 5) {
                        pingCounter = 0;
                        pingBridge();
                    }

                } catch (InterruptedException e) {
                    break;

                } catch (Throwable ignored) {
                }
            }
        }, "StormDaemonWatchdog");

        watchdogThread.start();
    }

    private void stopWatchdog() {
        if (watchdogThread != null) {
            watchdogThread.interrupt();
            watchdogThread = null;
        }
    }

    private void onDaemonDead(String reason) {
        try {
            Thread.sleep(restartBackoffMs);
        } catch (InterruptedException e) {
            return;
        }

        restartBackoffMs = Math.min(restartBackoffMs * 2, 20000);

        if (rootProcess != null) {
            try {
                rootProcess.destroy();
            } catch (Throwable ignored) {
            }
            rootProcess = null;
        }

        if (!checkRoot()) {
            return;
        }

        new Thread(() -> {
            try {
                startStormDaemon();
                restartBackoffMs = 3000;
            } catch (Throwable ignored) {
            }
        }, "StormDaemonRestart").start();
    }

    // ==================================================================
    // 7) PING NA PONTE (android.net.LocalSocket -> unix socket do daemon)
    // ==================================================================

    private boolean pingBridge() {
        LocalSocket socket = null;

        try {
            socket = new LocalSocket();

            socket.connect(new LocalSocketAddress(
                    bridgeSocket,
                    LocalSocketAddress.Namespace.FILESYSTEM));

            socket.setSoTimeout(1500);

            OutputStream out = socket.getOutputStream();
            InputStream in = socket.getInputStream();

            // BridgeRequest: magic, version, cmd=PING, seq, pid, payloadSize, address, size, reserved
            ByteBuffer req = ByteBuffer.allocate(40).order(ByteOrder.LITTLE_ENDIAN);
            req.putInt(BRIDGE_MAGIC);            // Magic
            req.putInt(BRIDGE_PROTO_VERSION);    // Version
            req.putInt(BRIDGE_CMD_PING);         // Cmd
            req.putInt(1);                       // Seq
            req.putInt(0);                       // Pid
            req.putInt(0);                       // PayloadSize
            req.putLong(0);                      // Address
            req.putInt(0);                       // Size
            req.putInt(0);                       // Reserved

            out.write(req.array());
            out.flush();

            ByteBuffer resp = ByteBuffer.allocate(32).order(ByteOrder.LITTLE_ENDIAN);
            byte[] buf = new byte[32];
            int total = 0;

            while (total < 32) {
                int n = in.read(buf, total, 32 - total);
                if (n < 0) break;
                total += n;
            }

            if (total != 32) {
                return false;
            }

            resp.put(buf);
            resp.flip();

            int magic = resp.getInt();
            int status = resp.getInt(16);
            long value = resp.getLong(24);

            return magic == BRIDGE_MAGIC && status == 0 && value == BRIDGE_PROTO_VERSION;

        } catch (Throwable e) {
            return false;

        } finally {
            try {
                if (socket != null) socket.close();
            } catch (Throwable ignored) {
            }
        }
    }

    // ==================================================================
    // 8) PARAR O DAEMON + LIMPEZA DE RASTROS (novos e LEGADOS)
    // ==================================================================

    private void stopStormDaemon() {
        // Mata pelo pidfile atual
        runAsRoot(
                "if [ -f " + shellQuote(bridgePidfile) + " ]; then " +
                "kill -TERM $(cat " + shellQuote(bridgePidfile) + ") 2>/dev/null; " +
                "sleep 0.3; " +
                "kill -KILL $(cat " + shellQuote(bridgePidfile) + ") 2>/dev/null; " +
                "fi; " +
                // Mata pelo comm do masquerade (daemon novo)
                "pkill -TERM appstats 2>/dev/null; " +
                // Mata o daemon LEGADO (versao antiga, nome fixo)
                "pkill -TERM stormdaemon 2>/dev/null; " +
                "true");

        // Apaga rastros: atuais e legados (nomes fixos da versao antiga)
        runAsRoot(
                "rm -f " + shellQuote(bridgePidfile) + " " + shellQuote(bridgeSocket) +
                " /data/local/tmp/stormbridge.sock" +
                " /data/local/tmp/stormbridge.pid" +
                " /data/local/tmp/stormbridge.log" +
                " /data/local/tmp/stormdaemon" +
                " /data/local/tmp/libc++_shared.so" +
                " ; true");

        if (rootProcess != null) {
            try {
                rootProcess.destroy();
            } catch (Throwable ignored) {
            }

            rootProcess = null;
        }
    }

    // ==================================================================
    // 9) DRENO DA SAÍDA DO PROCESSO ROOT (sem logar nada)
    // ==================================================================

    private void drainProcessOutput(Process process) {
        try {
            InputStream input = process.getInputStream();
            byte[] buffer = new byte[4096];

            //noinspection StatementWithEmptyBody
            while (input.read(buffer) > 0) {
                // drena e descarta
            }

        } catch (Throwable ignored) {
        }
    }

    // ==================================================================
    // UTIL
    // ==================================================================

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }
}
