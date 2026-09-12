package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
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
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * ============================================================================
 * DaemonService - GERENCIADOR DO DAEMON-PONTE (root)
 * ============================================================================
 *
 * Responsabilidades deste serviço (TUDO logado, tag "StormDaemonMgr"):
 *
 *  1. Verificar acesso root (su).
 *  2. Extrair o executável "stormdaemon" do APK (assets/bin/<abi>/) e
 *     instalá-lo em /data/local/tmp/stormdaemon via su (cp + chmod 755).
 *  3. Copiar libc++_shared.so para /data/local/tmp (o daemon precisa dele
 *     para iniciar fora do linker do app).
 *  4. Iniciar o daemon como root: su -c "exec /data/local/tmp/stormdaemon".
 *     O daemon abre o socket /data/local/tmp/stormbridge.sock e fica
 *     escutando os pedidos READ/WRITE do client (libclient.so).
 *  5. Monitorar: watchdog verifica o processo a cada 3s e reinicia com
 *     backoff se morrer; a cada 15s manda um PING pela ponte e loga a
 *     saúde dela.
 *  6. Parar: kill -TERM/-KILL + limpeza de socket/pid ao destruir o serviço.
 *
 * O daemon é SOMENTE PONTE de read/write - nada de lógica de jogo aqui.
 * ============================================================================
 */
public class DaemonService extends Service {

    private static final String TAG = "StormDaemonMgr";

    private static final String CHANNEL_ID = "storm_daemon_channel";

    // ------------------------------------------------------------------
    // Caminhos fixos (mesmos do daemon_main.cpp e do BridgeClient.cpp)
    // ------------------------------------------------------------------

    private static final String REMOTE_DIR = "/data/local/tmp";

    private static final String REMOTE_DAEMON = REMOTE_DIR + "/stormdaemon";

    private static final String REMOTE_CPP_SHARED = REMOTE_DIR + "/libc++_shared.so";

    private static final String BRIDGE_SOCKET = REMOTE_DIR + "/stormbridge.sock";

    private static final String BRIDGE_PIDFILE = REMOTE_DIR + "/stormbridge.pid";

    private static final String BRIDGE_LOGFILE = REMOTE_DIR + "/stormbridge.log";

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
        Log.i(TAG, "DaemonService iniciado (startId=" + startId + ")");

        startForeground(2, buildNotification());

        if (installing.compareAndSet(false, true)) {
            new Thread(this::installAndStart, "StormDaemonInstaller").start();
        } else {
            Log.i(TAG, "Instalação já em andamento - ignorando onStartCommand duplicado");
        }

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "DaemonService sendo destruído - parando daemon");

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
                .setContentTitle("Storm Daemon")
                .setContentText("Ponte root ativa (/data/local/tmp/stormdaemon)")
                .setSmallIcon(android.R.drawable.ic_menu_manage)
                .setOngoing(true)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Storm Daemon",
                    NotificationManager.IMPORTANCE_LOW);
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }

    // ==================================================================
    // 1) INSTALAÇÃO + INICIALIZAÇÃO
    // ==================================================================

    private void installAndStart() {
        try {
            Log.i(TAG, "────────────────────────────────────────");
            Log.i(TAG, "Instalando daemon-ponte em " + REMOTE_DIR);

            if (!checkRoot()) {
                Log.e(TAG, "SEM ROOT: su não retornou uid=0. O daemon-ponte não pode iniciar.");
                Log.e(TAG, "Dica: conceda acesso su ao app no seu gerenciador root (Magisk/SuperSU).");
                installing.set(false);
                return;
            }

            // Para qualquer instância anterior antes de sobrescrever arquivos
            stopStormDaemon();

            String abi = chooseAbi();
            Log.i(TAG, "ABI do daemon: " + abi);

            File localDaemon = extractAsset("bin/" + abi + "/stormdaemon", "stormdaemon");
            validateFile(localDaemon, "stormdaemon (" + abi + ")");

            File localCppShared = extractNativeLibrary(abi, "libc++_shared.so");
            validateFile(localCppShared, "libc++_shared.so");

            if (!installFiles(localDaemon, localCppShared)) {
                Log.e(TAG, "Falha instalando arquivos em " + REMOTE_DIR);
                installing.set(false);
                return;
            }

            startStormDaemon();
            startWatchdog();

            started = true;
            installing.set(false);

            Log.i(TAG, "────────────────────────────────────────");
            Log.i(TAG, "Daemon-ponte pronto. Socket: " + BRIDGE_SOCKET);
            Log.i(TAG, "Logs do daemon: logcat -s StormBridge  |  arquivo: " + BRIDGE_LOGFILE);
            Log.i(TAG, "────────────────────────────────────────");

        } catch (Throwable e) {
            Log.e(TAG, "Falha no installAndStart", e);
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

            Log.i(TAG, "su id -> exit=" + exitCode + " out='" + output + "'");

            boolean ok = exitCode == 0 && output.contains("uid=0");

            Log.i(TAG, "Root disponível: " + ok);

            return ok;

        } catch (Throwable e) {
            Log.e(TAG, "Erro verificando root", e);
            return false;
        }
    }

    private String runAsRoot(String command) {
        try {
            Log.d(TAG, "su -c: " + command);

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

            int code = process.waitFor();
            String result = out.toString().trim();

            if (code != 0 || !result.isEmpty()) {
                Log.d(TAG, "su result: exit=" + code + " out='" + result + "'");
            }

            return result;

        } catch (Throwable e) {
            Log.w(TAG, "runAsRoot falhou: " + e.getMessage());
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
     * A cópia para /data/local/tmp é feita depois VIA ROOT (o app não tem
     * permissão de escrever lá diretamente).
     */
    private File extractAsset(String assetPath, String name) throws Exception {
        File destination = new File(getFilesDir(), name + ".tmp");
        File finalFile = new File(getFilesDir(), name);

        Log.i(TAG, "Extraindo asset " + assetPath + " -> " + destination);

        try (InputStream in = getAssets().open(assetPath);
             FileOutputStream out = new FileOutputStream(destination, false)) {

            byte[] buffer = new byte[16 * 1024];
            int n;
            long total = 0;

            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
                total += n;
            }

            out.flush();

            Log.i(TAG, "Extraído: " + total + " bytes");
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

        Log.i(TAG, "Extraindo " + entryName + " do APK " + apkPath);

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

        Log.i(TAG, "OK: " + name + " (" + file.length() + " bytes em " + file.getAbsolutePath() + ")");
    }

    // ==================================================================
    // 4) INSTALAÇÃO EM /data/local/tmp (via su)
    // ==================================================================

    private boolean installFiles(File localDaemon, File localCppShared) {
        String qLocalDaemon = shellQuote(localDaemon.getAbsolutePath());
        String qLocalCpp = shellQuote(localCppShared.getAbsolutePath());
        String qDaemon = shellQuote(REMOTE_DAEMON);
        String qCpp = shellQuote(REMOTE_CPP_SHARED);

        String command =
                "mkdir -p " + shellQuote(REMOTE_DIR) +
                " && cat " + qLocalDaemon + " > " + qDaemon +
                " && chmod 755 " + qDaemon +
                " && chown root:root " + qDaemon +
                " && cat " + qLocalCpp + " > " + qCpp +
                " && chmod 644 " + qCpp +
                " && chmod 771 " + shellQuote(REMOTE_DIR) +
                " && ls -l " + qDaemon;

        String output = runAsRoot(command);

        // Confirma tamanho instalado = tamanho extraído
        try {
            long expected = localDaemon.length();
            String check = runAsRoot("stat -c %s " + qDaemon);

            if (!check.isEmpty()) {
                long installed = Long.parseLong(check.trim());
                if (installed != expected) {
                    Log.e(TAG, "Tamanho divergente: esperado=" + expected + " instalado=" + installed);
                    return false;
                }
                Log.i(TAG, "Binário instalado: " + REMOTE_DAEMON + " (" + installed + " bytes)");
            }
        } catch (Throwable e) {
            Log.w(TAG, "Não consegui confirmar tamanho instalado: " + e.getMessage());
        }

        return !output.contains("Permission denied");
    }

    // ==================================================================
    // 5) INICIAR O DAEMON COMO ROOT
    // ==================================================================

    private void startStormDaemon() {
        try {
            Log.i(TAG, "Iniciando daemon: " + REMOTE_DAEMON);

            // Limpa resquícios da execução anterior (o daemon também faz
            // unlink no bind, mas aqui garantimos estado limpo).
            runAsRoot("rm -f " + shellQuote(BRIDGE_SOCKET) + " " + shellQuote(BRIDGE_PIDFILE));

            String command =
                    "export LD_LIBRARY_PATH=" + REMOTE_DIR +
                    " && exec " + shellQuote(REMOTE_DAEMON) +
                    " --socket " + shellQuote(BRIDGE_SOCKET) +
                    " --pidfile " + shellQuote(BRIDGE_PIDFILE) +
                    " --log " + shellQuote(BRIDGE_LOGFILE);

            rootProcess = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();

            final Process process = rootProcess;

            // Saída do daemon -> logcat (tag StormDaemonMgr, prefixo [DAEMON])
            new Thread(() -> readProcessOutput(process, "DAEMON"), "StormDaemonLog").start();

            // Aguarda o socket aparecer (até 5s)
            for (int i = 0; i < 25; i++) {
                if (pingBridge()) {
                    Log.i(TAG, "Daemon-PONTE respondeu PING na tentativa " + (i + 1));
                    return;
                }

                Thread.sleep(200);
            }

            Log.w(TAG, "Daemon iniciado mas PING não respondeu em 5s (verifique logs [DAEMON] e 'logcat -s StormBridge')");

        } catch (Throwable e) {
            Log.e(TAG, "Falha iniciando daemon root", e);
        }
    }

    // ==================================================================
    // 6) WATCHDOG (monitorar + reiniciar + checar saúde da ponte)
    // ==================================================================

    private void startWatchdog() {
        stopWatchdog();

        watchdogThread = new Thread(() -> {
            Log.i(TAG, "Watchdog iniciado (intervalo=3s, ping da ponte a cada 15s)");

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
                        int exit = process.exitValue();

                        Log.e(TAG, "Daemon morreu: exit code=" + exit);
                        onDaemonDead("exit=" + exit);
                        continue;

                    } catch (IllegalThreadStateException alive) {
                        // Ainda rodando - ok
                    }

                    // PING de saúde a cada 5 ciclos (15s)
                    if (++pingCounter >= 5) {
                        pingCounter = 0;

                        boolean ok = pingBridge();

                        Log.i(TAG, "Healthcheck ponte: " + (ok ? "OK" : "SEM RESPOSTA") +
                                " (socket=" + BRIDGE_SOCKET + ")");
                    }

                } catch (InterruptedException e) {
                    Log.i(TAG, "Watchdog interrompido");
                    break;

                } catch (Throwable e) {
                    Log.w(TAG, "Watchdog erro: " + e.getMessage());
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
        Log.e(TAG, "Daemon-morto detectado (" + reason + ") - reiniciando em " + restartBackoffMs + "ms");

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
            Log.e(TAG, "Root perdido - não vou reiniciar o daemon");
            return;
        }

        Log.i(TAG, "Reiniciando daemon após morte");

        new Thread(() -> {
            try {
                startStormDaemon();
                restartBackoffMs = 3000;
            } catch (Throwable e) {
                Log.e(TAG, "Falha reiniciando daemon", e);
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
                    BRIDGE_SOCKET,
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
            int version = resp.getInt();
            int cmd = resp.getInt();
            int seq = resp.getInt();
            int status = resp.getInt();
            int payloadSize = resp.getInt();
            long value = resp.getLong();

            boolean ok = magic == BRIDGE_MAGIC && status == 0 && value == BRIDGE_PROTO_VERSION;

            Log.d(TAG, "PING resp: magic=0x" + Integer.toHexString(magic)
                    + " v" + version + " cmd=" + cmd + " seq=" + seq
                    + " status=" + status + " value=" + value);

            return ok;

        } catch (Throwable e) {
            Log.d(TAG, "PING falhou: " + e.getMessage());
            return false;

        } finally {
            try {
                if (socket != null) socket.close();
            } catch (Throwable ignored) {
            }
        }
    }

    // ==================================================================
    // 8) PARAR O DAEMON
    // ==================================================================

    private void stopStormDaemon() {
        Log.i(TAG, "Parando daemon (kill via su + limpeza de socket/pid)");

        try {
            runAsRoot(
                    "if [ -f " + shellQuote(BRIDGE_PIDFILE) + " ]; then " +
                    "kill -TERM $(cat " + shellQuote(BRIDGE_PIDFILE) + ") 2>/dev/null; " +
                    "sleep 0.5; " +
                    "kill -KILL $(cat " + shellQuote(BRIDGE_PIDFILE) + ") 2>/dev/null; " +
                    "fi; " +
                    "rm -f " + shellQuote(BRIDGE_PIDFILE) + " " + shellQuote(BRIDGE_SOCKET));

        } catch (Throwable e) {
            Log.w(TAG, "Erro no stop via su: " + e.getMessage());
        }

        if (rootProcess != null) {
            try {
                rootProcess.destroy();
            } catch (Throwable ignored) {
            }

            rootProcess = null;
        }
    }

    // ==================================================================
    // 9) LOG DA SAÍDA DO PROCESSO ROOT
    // ==================================================================

    private void readProcessOutput(Process process, String prefix) {
        try {
            InputStream input = process.getInputStream();
            byte[] buffer = new byte[4096];
            StringBuilder pending = new StringBuilder();

            int n;

            while ((n = input.read(buffer)) > 0) {
                pending.append(new String(buffer, 0, n));

                int newline;

                while ((newline = pending.indexOf("\n")) >= 0) {
                    String line = pending.substring(0, newline).trim();
                    pending.delete(0, newline + 1);

                    if (!line.isEmpty()) {
                        Log.i(TAG, "[" + prefix + "] " + line);
                    }
                }
            }

        } catch (Throwable e) {
            Log.w(TAG, "Leitor de log do processo encerrado: " + e.getMessage());
        }
    }

    // ==================================================================
    // UTIL
    // ==================================================================

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }
}
