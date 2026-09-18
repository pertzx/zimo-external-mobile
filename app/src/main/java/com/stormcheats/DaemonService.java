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

import java.io.ByteArrayOutputStream;
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
 * DaemonService - GERENCIADOR DO DAEMON-PONTE (root) — STEALTH + DIAGNÓSTICO
 * ============================================================================
 *
 * STEALTH (anti-ban) — mantido:
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
 *
 * DIAGNOSTICO (NOVO — motivo desta versao):
 *  - A versao anterior engolia TODOS os erros (catch Throwable ignored).
 *    Resultado: se o root nao estava concedido, se o asset nao existia, se o
 *    daemon morria no boot ou o SELinux bloqueava o connect, o app ficava
 *    MUDO e o usuario so via "[INIT] FALHA / restart async" do lado do jogo.
 *  - Agora CADA etapa loga o resultado com o tag StormDaemonMgr (logcat do
 *    PROPRIO app — o jogo nao consegue ler logcat de outro UID, entao nao
 *    vaza nada pra ele).
 *  - Root negado -> retry automatico a cada 10s (basta conceder no Magisk
 *    com o app aberto, sem reinstalar).
 *  - Daemon morre no boot -> a saida do processo root (erro do linker,
 *    "CANNOT LINK EXECUTABLE", "not executable", etc) e capturada e logada.
 *  - Daemon vivo mas socket mudo -> checa bind no /proc/net/unix e aplica
 *    regras SELinux minimas via magiskpolicy (so permitem O PROPRIO app
 *    conectar no socket) e repete o ping.
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

    // PLANO-B: socket DENTRO do dir privado do app (bypass total do
    // /data/local/tmp + SELinux shell_data_file). Root pode criar aqui;
    // o app conecta no PROPRIO dir (nunca bloqueado por policy).
    private String appDirSocket;   // <filesDir>/b
    private String appDirPidfile;  // <filesDir>/p

    private static final String PATH_FILE_NAME = "stormbridge.path";

    private static final String PREFS = "sys_prefs";
    private static final String PREF_TOKEN = "sys_token";
    private static final String PREF_APPSOCK = "sys_appsock";

    private static final int BRIDGE_MAGIC = 0x53544F52; // "STOR"

    private static final int BRIDGE_PROTO_VERSION = 1;

    private static final int BRIDGE_CMD_PING = 1;

    // ------------------------------------------------------------------

    private final AtomicBoolean installing = new AtomicBoolean(false);

    private volatile boolean started = false;

    private volatile boolean stopped = false;

    /**
     * true quando o ping da ponte ja respondeu pelo menos uma vez nesta
     * rodada de startStormDaemon(). O instalador usa isso pra decidir se
     * o resultado final e "[OK] PONTE PRONTA" ou "[5/5] FALHOU".
     */
    private volatile boolean bridgeReady = false;

    private volatile Process rootProcess;

    private Thread watchdogThread;

    private Thread installerThread;

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
            stopped = false;
            installerThread = new Thread(this::installLoop, "StormDaemonInstaller");
            installerThread.start();
        } else {
            Log.d(TAG, "instalacao ja em andamento - onStartCommand ignorado");
        }

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopped = true;

        stopWatchdog();

        Thread installer = installerThread;
        if (installer != null) {
            installer.interrupt();
        }

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

        appDirSocket = new File(getFilesDir(), "b").getAbsolutePath();
        appDirPidfile = new File(getFilesDir(), "p").getAbsolutePath();

        // Sessao anterior precisou do PLANO-B (SELinux negando o
        // /data/local/tmp): ja comeca nele, sem pagar 5s de tentativa
        // remota pra descobrir o mesmo bloqueio de novo.
        if (getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_APPSOCK, false)) {
            bridgeSocket = appDirSocket;
            bridgePidfile = appDirPidfile;
            Log.i(TAG, "[0/5] socket no dir PRIVADO do app (plano-B "
                    + "persistido da sessao anterior): " + bridgeSocket);
        }

        // Publica o path do socket pro client (libclient.so le daqui).
        // Arquivo PRIVADO do app — o jogo nao consegue ler.
        try (FileOutputStream fo = openFileOutput(PATH_FILE_NAME, MODE_PRIVATE)) {
            fo.write(bridgeSocket.getBytes("UTF-8"));
            fo.flush();
        } catch (Throwable ignored) {
        }
    }

    // ==================================================================
    // LOOP DE INSTALAÇÃO (com diagnostico e retry)
    // ==================================================================
    /*
     * Fluxo por tentativa:
     *   [1/5] root      -> su -c id (retry 10s enquanto negado)
     *   [2/5] limpeza   -> mata daemon anterior + apaga rastros
     *   [3/5] extracao  -> asset bin/<abi>/stormdaemon + lib/<abi>/libc++_shared.so
     *   [4/5] instalacao-> copia pro dir oculto via su + verificacao de tamanho
     *   [5/5] start     -> exec como root + ping (com auto-fix SELinux)
     *   [OK ] ponte pronta
     * Qualquer erro: loga o PASSO exato e o motivo, espera e tenta de novo.
     */

    private void installLoop() {
        initPaths();

        Log.i(TAG, "[0/5] instalacao iniciada (dir oculto: " + remoteDir + ")");

        boolean loggedRootHelp = false;

        while (!stopped) {
            try {
                // ----------------------------------------------------------
                // [1/5] ROOT
                // ----------------------------------------------------------
                if (!checkRoot()) {
                    if (!loggedRootHelp) {
                        Log.e(TAG, "[1/5] ROOT indisponivel - conceda Superuser "
                                + "a este app no gerenciador root (Magisk/KernelSU). "
                                + "Tentando a cada 10s (nao precisa reinstalar).");
                        Log.e(TAG, "[1/5] which su: "
                                + runAsRoot("command -v su || echo NAO_ENCONTRADO"));
                        loggedRootHelp = true;
                    }

                    Thread.sleep(10000);
                    continue;
                }

                Log.i(TAG, "[1/5] root OK (uid=0 confirmado)");

                // ----------------------------------------------------------
                // [2/5] LIMPEZA
                // ----------------------------------------------------------
                stopStormDaemon();
                Log.i(TAG, "[2/5] daemon anterior parado + rastros apagados");

                // ----------------------------------------------------------
                // [3/5] EXTRACAO DO APK
                // ----------------------------------------------------------
                String abi = chooseAbi();
                Log.i(TAG, "[3/5] ABI selecionada: " + abi);

                File localDaemon = extractAsset("bin/" + abi + "/stormdaemon", "stormdaemon");
                validateFile(localDaemon, "stormdaemon (" + abi + ")");

                File localCppShared = extractNativeLibrary(abi, "libc++_shared.so");
                validateFile(localCppShared, "libc++_shared.so");

                Log.i(TAG, "[3/5] extraidos: stormdaemon=" + localDaemon.length()
                        + " bytes | libc++_shared.so=" + localCppShared.length() + " bytes");

                // ----------------------------------------------------------
                // [4/5] INSTALACAO NO DIR OCULTO (via root)
                // ----------------------------------------------------------
                if (!installFiles(localDaemon, localCppShared)) {
                    Log.e(TAG, "[4/5] instalacao em " + remoteDir
                            + " FALHOU (su executou os comandos?) - tentando de novo em 15s");
                    Thread.sleep(15000);
                    continue;
                }

                Log.i(TAG, "[4/5] arquivos instalados e verificados em " + remoteDir);

                // ----------------------------------------------------------
                // [5/5] START + PING (com diagnostico profundo se falhar)
                // ----------------------------------------------------------
                startStormDaemon();

                startWatchdog();
                started = true;

                if (bridgeReady) {
                    Log.i(TAG, "[OK] PONTE PRONTA em " + bridgeSocket
                            + " - daemon root ativo (daemon mudo, sem rastro)");
                } else {
                    Log.e(TAG, "[5/5] daemon NAO respondeu ao ping - veja as linhas "
                            + "[5/5] acima para o motivo exato. Watchdog continua "
                            + "tentando reviver em background.");
                }

                // Instalador concluido (sucesso ou nao): o watchdog assume.
                return;

            } catch (InterruptedException ie) {
                return;

            } catch (Throwable e) {
                String msg = e.getMessage();

                Log.e(TAG, "[ERRO] instalacao parou: "
                        + e.getClass().getSimpleName()
                        + (msg == null || msg.isEmpty() ? "" : (": " + msg))
                        + " - tentando de novo em 15s");

                try {
                    Thread.sleep(15000);
                } catch (InterruptedException ignored) {
                    return;
                }
            }
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

            boolean ok = exitCode == 0 && output.contains("uid=0");

            if (!ok) {
                Log.w(TAG, "[1/5] su respondeu exit=" + exitCode + " out='"
                        + output + "' (permissao negada?)");
            }

            return ok;

        } catch (Throwable e) {
            Log.w(TAG, "[1/5] su nao executou: " + e.getClass().getSimpleName());
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
    // 4) INSTALAÇÃO NO DIRETORIO OCULTO (via su) — COM VERIFICAÇÃO REAL
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

        String installOut = runAsRoot(command);

        if (!installOut.isEmpty()) {
            Log.w(TAG, "[4/5] comando de instalacao devolveu: " + installOut);
        }

        // Confirma tamanho instalado = tamanho extraído (verificacao REAL:
        // antes, stat vazio ou nao-numerico era tratado como sucesso).
        try {
            long expected = localDaemon.length();
            String check = runAsRoot("stat -c %s " + qDaemon);

            if (check.isEmpty()) {
                Log.e(TAG, "[4/5] stat nao devolveu nada - o su executou? "
                        + "binario instalado em " + remoteDaemon + "?");
                return false;
            }

            long installed = Long.parseLong(check.trim());

            if (installed != expected) {
                Log.e(TAG, "[4/5] tamanho divergente: instalado=" + installed
                        + " esperado=" + expected + " (copia pela su falhou)");
                return false;
            }
        } catch (NumberFormatException nfe) {
            Log.e(TAG, "[4/5] stat devolveu lixo: '"
                    + runAsRoot("stat -c %s " + qDaemon) + "' (ambiente root estranho)");
            return false;
        } catch (Throwable ignored) {
        }

        // Telemetria do ambiente (ajuda a diagnosticar SEM interagir com o jogo)
        String listing = runAsRoot("ls -l " + qDir);

        if (!listing.isEmpty()) {
            Log.i(TAG, "[4/5] dir oculto: " + listing.replace('\n', ' '));
        }

        String se = runAsRoot("getenforce");

        Log.i(TAG, "[4/5] SELinux: " + (se.isEmpty() ? "desconhecido" : se));

        return true;
    }

    // ==================================================================
    // 5) INICIAR O DAEMON COMO ROOT (mudo) + DIAGNOSTICO PROFUNDO
    // ==================================================================

    private void startStormDaemon() {
        bridgeReady = false;

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

            // Consome a saida SEM logar enquanto vivo; se o processo terminar,
            // a cauda (erro de linker, "not executable", crash) e logada.
            new Thread(() -> drainProcessOutput(process), "StormDaemonLog").start();

            // Aguarda o socket responder (até 5s)
            for (int i = 0; i < 25; i++) {
                if (pingBridge()) {
                    bridgeReady = true;
                    Log.i(TAG, "[5/5] ping OK (" + (i * 200) + "ms)");
                    return;
                }

                Thread.sleep(200);
            }

            // --------------------------------------------------------------
            // PING FALHOU — diagnóstico profundo
            // --------------------------------------------------------------
            boolean alive = true;
            int exitCode = -1;

            try {
                exitCode = process.exitValue();
                alive = false;
            } catch (IllegalThreadStateException stillRunning) {
                // vivo
            }

            if (!alive) {
                Log.e(TAG, "[5/5] daemon MORREU no boot (exit=" + exitCode
                        + "). Causas comuns: binario de outra ABI, libc++_shared.so "
                        + "nao carregou, SELinux bloqueou o exec. A saida do processo "
                        + "(se houver) aparece na linha '[daemon] saida'.");
            } else {
                Log.e(TAG, "[5/5] daemon VIVO mas socket nao responde "
                        + "(bind falhou ou connect do app bloqueado)");
            }

            // O socket esta bound? (via root, /proc/net/unix e confiavel)
            String bound = runAsRoot(
                    "grep -F " + shellQuote(bridgeSocket) + " /proc/net/unix | head -n 1");

            Log.e(TAG, "[5/5] socket no /proc/net/unix: "
                    + (bound.isEmpty() ? "NAO BOUND (daemon nao conseguiu criar o socket)"
                                       : "bound OK (" + bound.trim() + ")"));

            // Auto-fix SELinux: regras minimas que so permitem O PROPRIO app
            // conectar no socket da ponte. Nada mais e liberado.
            if (alive) {
                String mp = runAsRoot("command -v magiskpolicy || echo NAO_ENCONTRADO");

                if (!mp.isEmpty() && !mp.contains("NAO_ENCONTRADO")) {
                    // CORRECAO DA CAUSA RAIZ: as regras antigas valiam SO
                    // para o dominio "untrusted_app". Apps com targetSdk
                    // antigo, clones ou ROMs exotas caem em variantes
                    // (untrusted_app_27/30/32...) e a regra NAO batia -
                    // o SELinux continuava negando e a ponte ficava
                    // INDISPONIVEL. Agora detectamos o dominio REAL deste
                    // app via /proc/self/attr/current e aplicamos pra ele
                    // + todas as variantes historicas (idempotente).
                    String domain = getOwnSelinuxDomain();

                    Log.w(TAG, "[5/5] dominio SELinux do app: "
                            + (domain.isEmpty() ? "desconhecido" : domain));

                    java.util.LinkedHashSet<String> doms = new java.util.LinkedHashSet<>();
                    doms.add("untrusted_app");
                    doms.add("untrusted_app_25");
                    doms.add("untrusted_app_27");
                    doms.add("untrusted_app_30");
                    doms.add("untrusted_app_32");
                    doms.add("untrusted_app_34");
                    if (!domain.isEmpty()) doms.add(domain);

                    StringBuilder rb = new StringBuilder("magiskpolicy --live");

                    for (String d : doms) {
                        rb.append(" 'allow ").append(d)
                                .append(" shell_data_file:dir search'")
                                .append(" 'allow ").append(d)
                                .append(" shell_data_file:sock_file { open write }'")
                                .append(" 'allow ").append(d)
                                .append(" shell_data_file:file { open read getattr }'")
                                .append(" 'allow ").append(d)
                                .append(" magisk:unix_stream_socket connectto'");
                    }

                    Log.w(TAG, "[5/5] aplicando regras SELinux via magiskpolicy ("
                            + doms.size() + " dominios x 4 regras)...");

                    String rules = runAsRoot(rb.toString());

                    Log.w(TAG, "[5/5] magiskpolicy: "
                            + (rules.isEmpty() ? "regras aplicadas" : rules));

                    for (int i = 0; i < 10; i++) {
                        if (pingBridge()) {
                            bridgeReady = true;
                            Log.i(TAG, "[5/5] ping OK apos regras SELinux (era o SELinux bloqueando)");
                            return;
                        }

                        Thread.sleep(200);
                    }

                    Log.e(TAG, "[5/5] ainda sem resposta apos regras SELinux. "
                            + "Ativando PLANO-B (socket no dir privado do app)...");
                } else {
                    Log.e(TAG, "[5/5] magiskpolicy nao encontrado - seguindo para "
                            + "o PLANO-B (socket no dir privado do app)...");
                }

                // ----------------------------------------------------------
                // PLANO-B: daemon VIVO mas o app nao alcanca o socket em
                // /data/local/tmp (SELinux negando shell_data_file mesmo
                // com magiskpolicy — KernelSU, policy travada, ROM exotica).
                // Migramos o socket pro dir PRIVADO do app: o daemon root
                // cria la e o app conecta no proprio dir — SELinux NUNCA
                // nega app_data_file do proprio uid.
                // ----------------------------------------------------------
                if (!bridgeReady) {
                    tryAppDirSocketFallback();
                }
            }

        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();

        } catch (Throwable e) {
            Log.e(TAG, "[5/5] startStormDaemon erro: " + e.getClass().getSimpleName()
                    + (e.getMessage() == null ? "" : (": " + e.getMessage())));
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
        Log.w(TAG, "[watchdog] daemon morreu (" + reason
                + ") - reiniciando em " + restartBackoffMs + "ms");

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

        if (stopped) {
            return;
        }

        if (!checkRoot()) {
            Log.e(TAG, "[watchdog] root perdido/negado - daemon nao sera "
                    + "reiniciado ate o su voltar a funcionar");
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
    // 5b) PLANO-B: SOCKET NO DIRETORIO PRIVADO DO APP
    // ==================================================================
    /*
     * Sintoma: daemon root VIVO (bind OK em /data/local/tmp) mas o app
     * nao consegue connect (SELinux negando shell_data_file mesmo apos
     * magiskpolicy — KernelSU, policy travada, ROM exotica...).
     *
     * Solucao: reiniciar o daemon com --socket apontando pra DENTRO do
     * dir privado do app (<filesDir>/b). O daemon (root) cria o socket
     * la com chmod 666; o app conecta no PROPRIO diretorio — isso o
     * SELinux NUNCA nega (app_data_file do proprio uid). O caminho novo
     * e publicado no files/stormbridge.path e o client C++ re-le ao
     * falhar o connect (InvalidateResolvedSocketPath no BridgeClient).
     */

    private boolean tryAppDirSocketFallback() {
        Log.w(TAG, "[PLAN-B] migrando socket pro dir PRIVADO do app "
                + "(bypass do /data/local/tmp + SELinux)...");

        stopStormDaemon();

        bridgeSocket = appDirSocket;
        bridgePidfile = appDirPidfile;

        // Publica o novo caminho pro client C++ (ele re-le o arquivo
        // de path no proximo connect que falhar)
        try (FileOutputStream fo = openFileOutput(PATH_FILE_NAME, MODE_PRIVATE)) {
            fo.write(bridgeSocket.getBytes("UTF-8"));
            fo.flush();
        } catch (Throwable e) {
            Log.e(TAG, "[PLAN-B] falha gravando " + PATH_FILE_NAME + ": " + e);
            return false;
        }

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

            new Thread(() -> drainProcessOutput(process), "StormDaemonLogB").start();

            for (int i = 0; i < 25; i++) {
                if (pingBridge()) {
                    bridgeReady = true;

                    // Persiste: proximos boots ja comecam no dir privado
                    // (sem pagar a tentativa remota de novo)
                    getSharedPreferences(PREFS, MODE_PRIVATE)
                            .edit().putBoolean(PREF_APPSOCK, true).apply();

                    Log.i(TAG, "[PLAN-B] PONTE PRONTA no dir privado do app: "
                            + bridgeSocket + " (persistido)");
                    return true;
                }

                Thread.sleep(200);
            }

            boolean aliveB = true;
            try {
                process.exitValue();
                aliveB = false;
            } catch (IllegalThreadStateException ignored) {
            }

            String boundB = runAsRoot(
                    "grep -F " + shellQuote(bridgeSocket) + " /proc/net/unix | head -n 1");

            Log.e(TAG, "[PLAN-B] falhou: alive=" + aliveB
                    + " socket bound=" + (boundB.isEmpty() ? "NAO" : "SIM")
                    + " (watchdog continua tentando; veja '[daemon] saida' acima)");
            return false;

        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
            return false;
        } catch (Throwable e) {
            Log.e(TAG, "[PLAN-B] erro: " + e.getClass().getSimpleName()
                    + (e.getMessage() == null ? "" : (": " + e.getMessage())));
            return false;
        }
    }

    // ==================================================================
    // 5c) DOMINIO SELINUX DESTE APP (/proc/self/attr/current)
    // ==================================================================
    /*
     * Le o contexto SELinux do PROPRIO processo (o app sempre pode ler
     * o seu) e extrai o dominio:
     *   "u:r:untrusted_app_27:s0:c512,c768" -> "untrusted_app_27"
     * As regras do magiskpolicy passam a valer pro dominio REAL em vez
     * de chutar "untrusted_app" (que nao bate nas variantes).
     */
    private String getOwnSelinuxDomain() {
        java.io.FileInputStream fis = null;
        try {
            fis = new java.io.FileInputStream("/proc/self/attr/current");
            byte[] buf = new byte[128];
            int n = fis.read(buf);
            if (n <= 0) return "";
            String ctx = new String(buf, 0, n, "UTF-8").trim();
            int z = ctx.indexOf('\0');
            if (z >= 0) ctx = ctx.substring(0, z);
            String[] parts = ctx.split(":");
            return parts.length >= 3 ? parts[2] : "";
        } catch (Throwable e) {
            return "";
        } finally {
            try { if (fis != null) fis.close(); } catch (Throwable ignored) {}
        }
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
    // 9) DRENO DA SAÍDA DO PROCESSO ROOT (captura o erro de boot)
    // ==================================================================
    /*
     * Enquanto o daemon vive, a saida dele e vazia (ele e mudo e loga so no
     * logd com --verbose, que producao nao usa). Quando o daemon MORRE NO
     * BOOT, o dynamic linker imprime no stdout/stderr do processo:
     *   "CANNOT LINK EXECUTABLE ... library ... not found"
     *   "not executable" / "Permission denied"
     * Essa cauda e capturada e logada UMA vez, quando o stream fecha.
     */

    private void drainProcessOutput(Process process) {
        ByteArrayOutputStream tail = new ByteArrayOutputStream(2048);

        try {
            InputStream input = process.getInputStream();
            byte[] buffer = new byte[4096];

            int n;
            while ((n = input.read(buffer)) > 0) {
                if (tail.size() < 4096) {
                    tail.write(buffer, 0, n);
                }
            }

        } catch (Throwable ignored) {
        }

        String out = tail.toString().trim();

        if (!out.isEmpty()) {
            Log.e(TAG, "[daemon] saida do processo root (erro de linker/crash): " + out);
        }
    }

    // ==================================================================
    // UTIL
    // ==================================================================

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }
}
