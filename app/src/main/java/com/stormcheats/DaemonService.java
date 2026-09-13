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
 *  1. Verificar acesso root (su).
 *  2. Aplicar regras SELinux (magiskpolicy) para o APP poder conectar no
 *     socket do daemon. SEM ISSO o connect() volta "Permission denied"
 *     mesmo com o socket em chmod 666 (o Android bloqueia:
 *     untrusted_app -> unix_stream_socket connectto + sock_file write).
 *  3. Extrair o executável "stormdaemon" do APK e instalá-lo em
 *     /data/local/tmp/stormdaemon via su (cp + chmod 755).
 *  4. Iniciar o daemon como root: su -c "exec /data/local/tmp/stormdaemon".
 *  5. Monitorar: watchdog a cada 3s; PING na ponte a cada 15s; se o PING
 *     falhar, corrige permissões do socket (chmod 666 + restorecon) e
 *     tenta de novo (auto-cura) antes de reiniciar o daemon.
 *  6. Parar: kill -TERM/-KILL + limpeza de socket/pid ao destruir o serviço.
 *
 * Log: tag "StormDaemonMgr". Logs do próprio daemon: tag "StormBridge".
 * ============================================================================
 */
public class DaemonService extends Service {

    private static final String TAG = "StormDaemonMgr";

    private static final String CHANNEL_ID = "storm_daemon_channel";

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

    // Retry da instalação (zombie daemon, root demorando, etc)
    private int installRetries = 0;

    // Healthcheck: quantos PINGs seguidos falharam mesmo com auto-cura
    private int consecutiveBridgeFails = 0;

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
                scheduleInstallRetry();
                return;
            }

            // Mata QUALQUER stormdaemon antigo antes de mexer nos arquivos
            killAllDaemons();

            // CORREÇÃO DO "Permission denied": o app (untrusted_app) não pode
            // conectar no socket de um processo root enquanto o SELinux estiver
            // Enforcing sem regras. Aplica as regras ANTES de subir o daemon.
            applySelinuxPatch();

            String abi = chooseAbi();
            Log.i(TAG, "ABI do daemon: " + abi);

            File localDaemon = extractAsset("bin/" + abi + "/stormdaemon", "stormdaemon");
            validateFile(localDaemon, "stormdaemon (" + abi + ")");

            File localCppShared = extractNativeLibrary(abi, "libc++_shared.so");
            validateFile(localCppShared, "libc++_shared.so");

            if (!installFiles(localDaemon, localCppShared)) {
                Log.e(TAG, "Falha instalando arquivos em " + REMOTE_DIR);
                scheduleInstallRetry();
                return;
            }

            startStormDaemon();
            startWatchdog();

            started = true;
            installing.set(false);
            installRetries = 0;

            Log.i(TAG, "────────────────────────────────────────");
            Log.i(TAG, "Daemon-ponte pronto. Socket: " + BRIDGE_SOCKET);
            Log.i(TAG, "Logs do daemon: logcat -s StormBridge  |  arquivo: " + BRIDGE_LOGFILE);
            Log.i(TAG, "────────────────────────────────────────");

        } catch (Throwable e) {
            Log.e(TAG, "Falha no installAndStart", e);
            scheduleInstallRetry();
        }
    }

    // ==================================================================
    // 1.1) KILL ROBUSTO: mata qualquer instância antiga do daemon
    //
    //      - kill via pidfile (pode estar desatualizado)
    //      - killall + pkill por NOME (comm do processo é "stormdaemon")
    //      - kill -9 final via pgrep
    //
    //      Importante: NÃO usar "pkill -f" (o -f casa com a linha de
    //      comando e mataria o próprio shell do su que roda o comando).
    // ==================================================================

    private void killAllDaemons() {
        Log.i(TAG, "Matando qualquer stormdaemon antigo (pidfile + killall + pkill)");

        try {
            String command =
                    "if [ -f " + shellQuote(BRIDGE_PIDFILE) + " ]; then " +
                    "kill -TERM $(cat " + shellQuote(BRIDGE_PIDFILE) + ") 2>/dev/null; " +
                    "fi; " +
                    "killall stormdaemon 2>/dev/null; " +
                    "pkill stormdaemon 2>/dev/null; " +
                    "sleep 0.5; " +
                    "kill -9 $(pgrep stormdaemon) 2>/dev/null; " +
                    "rm -f " + shellQuote(BRIDGE_PIDFILE) + " " + shellQuote(BRIDGE_SOCKET);

            runAsRoot(command);

        } catch (Throwable e) {
            Log.w(TAG, "Erro no killAllDaemons: " + e.getMessage());
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
    // 1.2) RETRY DA INSTALAÇÃO
    // ==================================================================

    private void scheduleInstallRetry() {
        installRetries++;

        if (installRetries > 5) {
            Log.e(TAG, "Desistindo do retry de instalação após 5 tentativas");
            installing.set(false);
            return;
        }

        Log.w(TAG, "Agendando reinstalação em 15s (tentativa " + installRetries + "/5)");
        installing.set(false);

        new Thread(() -> {
            try {
                Thread.sleep(15000);
            } catch (InterruptedException e) {
                return;
            }

            if (installing.compareAndSet(false, true)) {
                Log.i(TAG, "RETRY de instalação do daemon (" + installRetries + "/5)");
                installAndStart();
            }
        }, "StormDaemonRetry").start();
    }

    // ==================================================================
    // 2) PATCH SELINUX (causa raiz do "Permission denied")
    //
    //    O socket do daemon vive em /data/local/tmp (label shell_data_file)
    //    e o daemon roda como root (domínio su/magisk). O app roda em
    //    untrusted_app. A policy padrão do Android NÃO permite:
    //
    //      - untrusted_app shell_data_file:sock_file write  (connect no socket)
    //      - untrusted_app <root>:unix_stream_socket connectto
    //
    //    Resultado: connect() volta EACCES ("Permission denied") tanto no
    //    client C++ quanto no PING Java, mesmo com o socket em chmod 666.
    //
    //    Correção: magiskpolicy --live (Magisk) ou supolicy (SuperSU).
    //    Fallback: setenforce 0.
    // ==================================================================

    private boolean applySelinuxPatch() {
        try {
            String before = runAsRoot("getenforce").trim();
            Log.i(TAG, "SELinux atual: '" + before + "'");

            if (!before.isEmpty() && !before.contains("Enforcing")) {
                Log.i(TAG, "SELinux Permissive - nenhum patch necessário");
                return true;
            }

            /*
             * O app pode cair em domínios diferentes conforme targetSdk/ABI.
             * Aplica para as variantes untrusted_app conhecidas. O wildcard
             * "*" no alvo do connectto cobre qualquer domínio do daemon
             * (su, magisk, kernel...).
             */
            String[] domains = {
                    "untrusted_app",
                    "untrusted_app_27",
                    "untrusted_app_25",
                    "untrusted_app_32"
            };

            StringBuilder rules = new StringBuilder();

            for (String d : domains) {
                rules.append(" 'allow ").append(d).append(" shell_data_file sock_file write'");
                rules.append(" 'allow ").append(d).append(" shell_data_file dir search'");
                rules.append(" 'allow ").append(d).append(" * unix_stream_socket connectto'");
            }

            // 1) Magisk
            String out = runAsRoot("magiskpolicy --live" + rules);
            Log.i(TAG, "magiskpolicy: " + (out.isEmpty() ? "(ok, sem saída)" : out));

            if (!out.contains("not found") && !out.contains("No such file")) {
                Log.i(TAG, "Regras SELinux aplicadas via magiskpolicy");
                return true;
            }

            // 2) SuperSU
            out = runAsRoot("supolicy --live" + rules);
            Log.i(TAG, "supolicy: " + (out.isEmpty() ? "(ok, sem saída)" : out));

            if (!out.contains("not found") && !out.contains("No such file")) {
                Log.i(TAG, "Regras SELinux aplicadas via supolicy");
                return true;
            }

            // 3) Último recurso: modo permissivo
            Log.e(TAG, "magiskpolicy/supolicy indisponíveis - usando setenforce 0");
            runAsRoot("setenforce 0");

            String after = runAsRoot("getenforce").trim();
            Log.w(TAG, "SELinux agora: '" + after + "'");

            return true;

        } catch (Throwable e) {
            Log.e(TAG, "Erro no patch SELinux", e);
            return false;
        }
    }

    // ==================================================================
    // 2.1) CORREÇÃO DE PERMISSÕES DO SOCKET (auto-cura)
    //
    //      Se o daemon em execução for uma versão antiga (sem chmod 666
    //      no bind) ou o socket ficou com dono/mode errados, o app também
    //      recebe Permission denied. Isso conserta sem reiniciar nada.
    // ==================================================================

    private void fixSocketPermissions() {
        try {
            String qSock = shellQuote(BRIDGE_SOCKET);
            String qDir = shellQuote(REMOTE_DIR);

            String command =
                    "if [ -S " + qSock + " ]; then " +
                    "chmod 666 " + qSock + "; " +
                    "chown root:root " + qSock + "; " +
                    "restorecon " + qSock + " 2>/dev/null; " +
                    "fi; " +
                    "chmod 771 " + qDir + " 2>/dev/null; " +
                    "ls -l " + qSock + " 2>/dev/null";

            String out = runAsRoot(command);

            if (!out.isEmpty()) {
                Log.i(TAG, "fixSocketPermissions: " + out.replace("\n", " | "));
            }

        } catch (Throwable e) {
            Log.w(TAG, "fixSocketPermissions falhou: " + e.getMessage());
        }
    }

    // ==================================================================
    // 4) INSTALAÇÃO EM /data/local/tmp (via su)
    //
    //    A correção do "Text file busy": rm -f ANTES do cat.
    //    Não se pode ESCREVER num binário em execução (ETXTBSY), mas
    //    pode REMOVER a entrada dele (rm) e criar um arquivo novo no
    //    lugar — o cat cria um inode novo e o ETXTBSY desaparece.
    // ==================================================================

    private boolean installFiles(File localDaemon, File localCppShared) {
        killAllDaemons();

        String qLocalDaemon = shellQuote(localDaemon.getAbsolutePath());
        String qLocalCpp = shellQuote(localCppShared.getAbsolutePath());
        String qDaemon = shellQuote(REMOTE_DAEMON);
        String qCpp = shellQuote(REMOTE_CPP_SHARED);

        String command =
                "mkdir -p " + shellQuote(REMOTE_DIR) +
                " && rm -f " + qDaemon + " " + qCpp +
                " && cat " + qLocalDaemon + " > " + qDaemon +
                " && chmod 755 " + qDaemon +
                " && chown root:root " + qDaemon +
                " && cat " + qLocalCpp + " > " + qCpp +
                " && chmod 644 " + qCpp +
                " && chmod 771 " + shellQuote(REMOTE_DIR) +
                " && ls -l " + qDaemon;

        boolean installed = false;
        String output = "";

        for (int attempt = 1; attempt <= 3 && !installed; attempt++) {
            if (attempt > 1) {
                Log.w(TAG, "Tentativa " + attempt + " de instalação (kill again + retry)");
                killAllDaemons();
            }

            output = runAsRoot(command);

            if (output.contains("Text file busy")) {
                Log.e(TAG, "Text file busy - daemon antigo ainda rodando, matando e tentando de novo");
                continue;
            }

            installed = true;
        }

        if (!installed) {
            Log.e(TAG, "Instalação falhou após 3 tentativas. Última saída: " + output);
            return false;
        }

        if (output.contains("Permission denied")) {
            Log.e(TAG, "Permission denied instalando em " + REMOTE_DIR);
            return false;
        }

        // Confirma tamanho instalado = tamanho extraído
        try {
            long expected = localDaemon.length();
            String check = runAsRoot("stat -c %s " + qDaemon);

            if (!check.isEmpty()) {
                long installedSize = Long.parseLong(check.trim());
                if (installedSize != expected) {
                    Log.e(TAG, "Tamanho divergente: esperado=" + expected + " instalado=" + installedSize);
                    return false;
                }
                Log.i(TAG, "Binário instalado: " + REMOTE_DAEMON + " (" + installedSize + " bytes)");
            }
        } catch (Throwable e) {
            Log.w(TAG, "Não consegui confirmar tamanho instalado: " + e.getMessage());
        }

        return true;
    }

    // ==================================================================
    // 3) ROOT
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
    // 3.1) ABI + EXTRAÇÃO DO APK
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

            // Aguarda o socket aparecer (até 5s). Se metade do caminho o PING
            // ainda não responder, corrige permissões do socket e segue.
            boolean chmodTried = false;

            for (int i = 0; i < 25; i++) {
                if (pingBridge()) {
                    Log.i(TAG, "Daemon-PONTE respondeu PING na tentativa " + (i + 1));
                    return;
                }

                if (i == 12 && !chmodTried) {
                    chmodTried = true;
                    Log.w(TAG, "PING ainda sem resposta - corrigindo permissões do socket");
                    fixSocketPermissions();
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
            consecutiveBridgeFails = 0;

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

                        if (!ok) {
                            // AUTO-CURA: corrige permissões do socket e tenta
                            // mais uma vez antes de declarar a ponte morta.
                            Log.w(TAG, "Healthcheck falhou - aplicando fix de permissões e retestando");
                            fixSocketPermissions();
                            ok = pingBridge();

                            if (ok) {
                                Log.i(TAG, "AUTO-CURA funcionou: ponte respondeu após corrigir permissões");
                            }
                        }

                        if (ok) {
                            consecutiveBridgeFails = 0;
                            Log.i(TAG, "Healthcheck ponte: OK (socket=" + BRIDGE_SOCKET + ")");
                        } else {
                            consecutiveBridgeFails++;
                            Log.e(TAG, "Healthcheck ponte: SEM RESPOSTA (" + consecutiveBridgeFails + " seguidos)");

                            if (consecutiveBridgeFails >= 4) {
                                consecutiveBridgeFails = 0;
                                onDaemonDead("ponte sem resposta persistente");
                            }
                        }
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
                applySelinuxPatch();
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
        Log.i(TAG, "Parando daemon (kill robusto + limpeza de socket/pid)");
        killAllDaemons();
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