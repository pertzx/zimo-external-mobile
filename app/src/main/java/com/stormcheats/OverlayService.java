package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowMetrics;
import android.view.WindowManager;

public class OverlayService extends Service implements SurfaceHolder.Callback {

    private static final String TAG = "StormOverlay";
    private static final String CHANNEL_ID = "storm_overlay_channel";

    private WindowManager windowManager;

    private static final int TOUCH_PADDING_PX = 8;
    private volatile boolean isTouching = false;
    private float touchDownX = 0f;
    private float touchDownY = 0f;
    private boolean touchMoved = false;
    private static final float TOUCH_SLOP_DP = 8f;
    private float touchSlopPx;

    private SurfaceView surfaceView;
    private WindowManager.LayoutParams surfaceParams;

    private View touchView;
    private WindowManager.LayoutParams touchParams;

    private Thread renderThread;

    private int panelX = 0;
    private int panelY = 0;
    private int panelW = 300;
    private int panelH = 500;

    // ultima resolucao real aplicada na janela de render
    private int lastScreenW = 0;
    private int lastScreenH = 0;

    // ==================================================================
    // BOTÕES FLUTUANTES de keybind (FloatingKeys, lado C++)
    //
    // O C++ desenha os botões e publica os rects deles; o Java cria uma
    // PEQUENA janela de toque por cima de cada botão (a janela do painel
    // só cobre o painel - era por isso que os botões não recebiam toque).
    // O toque é repassado ao native: DOWN/UP -> nativeFloatingKeyTouch,
    // arrasto -> nativeFloatingKeyDrag + nativeFloatingKeyMove.
    // ==================================================================

    private static final int MAX_FLOATING_KEYS = 6;
    private static final int FK_PADDING_PX = 6;

    private static class KeyWindow {
        int vk;
        View view;
        WindowManager.LayoutParams params;
        int x, y, w, h;          // ultimo rect absoluto aplicado
        boolean added = false;   // janela atualmente no WindowManager
        // estado do gesto em andamento
        float lastRawX, lastRawY;
        float downRawX, downRawY;
        boolean dragging = false;
    }

    private final KeyWindow[] keyWindows = new KeyWindow[MAX_FLOATING_KEYS];
    private volatile boolean fkNativeOk = true; // libclient tem os símbolos novos?

    private final Handler panelTracker = new Handler(Looper.getMainLooper());
    private final Runnable trackPanelRunnable = new Runnable() {
        @Override
        public void run() {
            // se a tela girou (retrato -> paisagem no FreeFire),
            // a janela de render acompanha NA HORA
            syncScreenSize();
            syncPanelBoundsFromNative();
            syncCaptureBypassFromNative();
            syncFloatingKeys();
            panelTracker.postDelayed(this, 16);
        }
    };

    static {
        try {
            System.loadLibrary("client");
            Log.i(TAG, "libclient.so carregada");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Falha ao carregar libclient.so: " + e.getMessage());
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();

        createNotificationChannel();
        startForeground(1, buildNotification());

        try {
            createOverlay();
            panelTracker.post(trackPanelRunnable);
        } catch (Throwable t) {
            Log.e(TAG, "Falha ao criar overlay", t);
            stopSelf();
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startForeground(1, buildNotification());
        return START_STICKY;
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // rotacao do display: reapply tamanho real imediatamente
        syncScreenSize();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        panelTracker.removeCallbacks(trackPanelRunnable);
        nativeStopPanel();
        removeAllKeyWindows();
        try { if (surfaceView != null) windowManager.removeView(surfaceView); } catch (Exception ignored) {}
        try { if (touchView != null) windowManager.removeView(touchView); } catch (Exception ignored) {}
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    /**
     * Retorna o tamanho REAL do display (incluindo a area do notch/cutout e a
     * barra de status).
     */
    private int[] getRealScreenSize() {
        Point real = new Point(0, 0);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                WindowMetrics metrics = windowManager.getMaximumWindowMetrics();
                real.x = metrics.getBounds().width();
                real.y = metrics.getBounds().height();
            } else {
                Display disp = windowManager.getDefaultDisplay();
                if (disp != null) disp.getRealSize(real);
            }
        } catch (Throwable t) {
            Log.w(TAG, "getRealScreenSize falhou: " + t.getMessage());
        }
        if (real.x <= 0 || real.y <= 0) {
            Display disp = windowManager.getDefaultDisplay();
            if (disp != null) disp.getSize(real);
        }
        return new int[]{ real.x, real.y };
    }

    /**
     * CORRECAO DO OVERLAY CORTADO: reavalia o tamanho real da tela e atualiza
     * a janela de render quando mudou (rotacao, dobraveis, etc).
     */
    private boolean syncScreenSize() {
        if (surfaceView == null || surfaceParams == null || windowManager == null) return false;

        int[] real = getRealScreenSize();

        if (real[0] == lastScreenW && real[1] == lastScreenH) return false;

        lastScreenW = real[0];
        lastScreenH = real[1];

        surfaceParams.width = real[0];
        surfaceParams.height = real[1];
        try {
            windowManager.updateViewLayout(surfaceView, surfaceParams);
            Log.i(TAG, "Screen size sync: " + real[0] + "x" + real[1]);
        } catch (Exception e) {
            Log.w(TAG, "syncScreenSize falhou: " + e.getMessage());
        }
        return true;
    }

    /*
     * STREAM MODE (Task 12) — estado local espelhado do CaptureBypass.
     * Comeca true (padrao da config): a janela de render JA NASCE com
     * FLAG_SECURE em createRenderWindow().
     */
    private boolean lastCaptureBypass = true;
    private boolean captureBypassInit = false;

    /**
     * FIX DO STREAM MODE: o checkbox "Stream Mode" (CaptureBypass) existia,
     * era salvo... e NUNCA era aplicado em lugar nenhum. Agora o painel
     * ImGui responde o estado via nativeShouldCaptureBypass() e este loop
     * aplica/remove FLAG_SECURE na janela de render: com o modo ligado a
     * ESP some de prints, gravacao de tela e lives (stream-proof real).
     */
    private void syncCaptureBypassFromNative() {
        if (!fkNativeOk) return;

        try {
            boolean want = nativeShouldCaptureBypass();

            if (captureBypassInit && want == lastCaptureBypass) return;

            captureBypassInit = true;
            lastCaptureBypass = want;

            if (surfaceView == null || surfaceParams == null || windowManager == null) return;

            int newFlags = surfaceParams.flags & ~WindowManager.LayoutParams.FLAG_SECURE;
            if (want) newFlags |= WindowManager.LayoutParams.FLAG_SECURE;

            if (newFlags != surfaceParams.flags) {
                surfaceParams.flags = newFlags;
                windowManager.updateViewLayout(surfaceView, surfaceParams);
                Log.i(TAG, "Stream Mode: FLAG_SECURE " + (want ? "ON" : "OFF"));
            }

            // janela de TOQUE (painel ImGui) entra no stream-proof tambem
            if (touchView != null && touchParams != null) {
                int tf = touchParams.flags & ~WindowManager.LayoutParams.FLAG_SECURE;
                if (want) tf |= WindowManager.LayoutParams.FLAG_SECURE;
                if (tf != touchParams.flags) {
                    touchParams.flags = tf;
                    try { windowManager.updateViewLayout(touchView, touchParams); } catch (Exception ignored) {}
                }
            }

            // janelas dos botoes flutuantes idem
            for (KeyWindow kw : keyWindows) {
                if (kw == null || kw.view == null || kw.params == null || !kw.added) continue;
                int kf = kw.params.flags & ~WindowManager.LayoutParams.FLAG_SECURE;
                if (want) kf |= WindowManager.LayoutParams.FLAG_SECURE;
                if (kf != kw.params.flags) {
                    kw.params.flags = kf;
                    try { windowManager.updateViewLayout(kw.view, kw.params); } catch (Exception ignored) {}
                }
            }
        } catch (Throwable e) {
            // simbolo antigo do libclient (sem o novo JNI): nao quebra o overlay
            fkNativeOk = false;
            Log.w(TAG, "syncCaptureBypassFromNative indisponivel: " + e.getMessage());
        }
    }

    /**
     * Aplica o modo de cutout que deixa a janela entrar na area do
     * notch/barra de status.
     */
    private void applyCutoutMode(WindowManager.LayoutParams params) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            params.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else {
            params.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
    }

    private void createOverlay() {
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        touchSlopPx = TOUCH_SLOP_DP * getResources().getDisplayMetrics().density;
        createRenderWindow();
        createTouchWindow();
    }

    private void createRenderWindow() {
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.setZOrderMediaOverlay(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        surfaceView.getHolder().addCallback(this);

        int flag = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        surfaceParams = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                flag,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT
        );
        surfaceParams.gravity = Gravity.TOP | Gravity.START;
        surfaceParams.x = 0;
        surfaceParams.y = 0;

        applyCutoutMode(surfaceParams);

        /*
         * STREAM MODE (Task 12): se o CaptureBypass ja esta ligado na
         * config, a janela de render NASCE com FLAG_SECURE (stream-proof
         * desde o primeiro frame; o toggle em runtime tambem existe).
         */
        if (lastCaptureBypass) {
            surfaceParams.flags |= WindowManager.LayoutParams.FLAG_SECURE;
        }

        int[] real = getRealScreenSize();
        lastScreenW = real[0];
        lastScreenH = real[1];
        surfaceParams.width = real[0];
        surfaceParams.height = real[1];

        windowManager.addView(surfaceView, surfaceParams);
    }

    private void createTouchWindow() {
        touchView = new View(this);
        touchView.setBackgroundColor(Color.TRANSPARENT);

        int flag = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        touchParams = new WindowManager.LayoutParams(
                panelW,
                panelH,
                flag,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT
        );
        touchParams.gravity = Gravity.TOP | Gravity.START;
        touchParams.x = panelX;
        touchParams.y = panelY;

        applyCutoutMode(touchParams);

        /* STREAM MODE: o painel tambem nasce stream-proof se ligado. */
        if (lastCaptureBypass) {
            touchParams.flags |= WindowManager.LayoutParams.FLAG_SECURE;
        }

        touchView.setFocusable(true);
        touchView.setFocusableInTouchMode(true);
        touchView.requestFocus();

        touchView.setOnTouchListener((v, event) -> {
            int action = event.getActionMasked();
            int pointerCount = event.getPointerCount();

            if (action == MotionEvent.ACTION_DOWN) {
                v.getParent().requestDisallowInterceptTouchEvent(true);
                isTouching = false;
                syncPanelBoundsFromNative();
                isTouching = true;

                touchDownX = event.getRawX(0);
                touchDownY = event.getRawY(0);
                touchMoved = false;
            }

            int[] loc = new int[2];
            surfaceView.getLocationOnScreen(loc);

            for (int i = 0; i < pointerCount; i++) {
                int pointerAction = action;

                if (pointerCount > 1
                        && action != MotionEvent.ACTION_DOWN
                        && action != MotionEvent.ACTION_UP
                        && action != MotionEvent.ACTION_CANCEL) {
                    int idx = (action & MotionEvent.ACTION_POINTER_INDEX_MASK)
                            >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
                    if (i != idx) pointerAction = MotionEvent.ACTION_MOVE;
                }

                float sx = event.getRawX(i) - loc[0];
                float sy = event.getRawY(i) - loc[1];

                if (pointerAction == MotionEvent.ACTION_MOVE && i == 0) {
                    if (!touchMoved) {
                        float dx = event.getRawX(0) - touchDownX;
                        float dy = event.getRawY(0) - touchDownY;
                        if (Math.hypot(dx, dy) > touchSlopPx) {
                            touchMoved = true;
                            nativeOnTouch(MotionEvent.ACTION_MOVE, sx, sy, event.getPointerId(i));
                        }
                        continue;
                    }
                }

                nativeOnTouch(pointerAction, sx, sy, event.getPointerId(i));
            }

            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                v.getParent().requestDisallowInterceptTouchEvent(false);
                isTouching = false;
                syncPanelBoundsFromNative();
                touchMoved = false;
            }

            if (action == MotionEvent.ACTION_UP) {
                int[] loc2 = new int[2];
                surfaceView.getLocationOnScreen(loc2);
                float ux = event.getRawX(0) - loc2[0];
                float uy = event.getRawY(0) - loc2[1];
                nativeOnTouch(MotionEvent.ACTION_MOVE, ux, uy, 0);
            }

            return true;
        });

        windowManager.addView(touchView, touchParams);
    }

    private void syncPanelBoundsFromNative() {
        if (touchView == null || isTouching) return;

        int[] b;
        try {
            b = nativeGetPanelBounds();
        } catch (Throwable t) {
            return;
        }

        if (b == null || b.length < 4 || b[2] <= 0 || b[3] <= 0) return;

        int[] loc = new int[2];
        surfaceView.getLocationOnScreen(loc);

        int absX = b[0] + loc[0];
        int absY = b[1] + loc[1];
        int absW = b[2];
        int absH = b[3];

        int newX = absX - TOUCH_PADDING_PX;
        int newY = absY - TOUCH_PADDING_PX;
        int newW = absW + TOUCH_PADDING_PX * 2;
        int newH = absH + TOUCH_PADDING_PX * 2;

        if (newX == panelX && newY == panelY && newW == panelW && newH == panelH) return;

        panelX = newX;
        panelY = newY;
        panelW = newW;
        panelH = newH;

        touchParams.x = panelX;
        touchParams.y = panelY;
        touchParams.width = panelW;
        touchParams.height = panelH;
        try { windowManager.updateViewLayout(touchView, touchParams); } catch (Exception ignored) {}
    }

    // ==================================================================
    // BOTÕES FLUTUANTES - janelas de toque por botão
    // ==================================================================

    /**
     * Consulta o native (a cada 16ms) onde estão os botões flutuantes e
     * cria/move/remove uma janelinha de toque para cada um.
     * Formato do array: [vk, x, y, w, h, vk, x, y, w, h, ...]
     * (coordenadas relativas à surfaceView)
     */
    private void syncFloatingKeys() {
        if (!fkNativeOk || surfaceView == null || windowManager == null) return;

        int[] keys;
        try {
            keys = nativeGetFloatingKeys();
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "nativeGetFloatingKeys indisponível - flutuantes desativados nesta build");
            fkNativeOk = false;
            return;
        } catch (Throwable t) {
            return;
        }

        int groups = (keys == null) ? 0 : keys.length / 5;

        int[] loc = new int[2];
        try {
            surfaceView.getLocationOnScreen(loc);
        } catch (Throwable ignored) {
            return;
        }

        boolean[] used = new boolean[MAX_FLOATING_KEYS];

        for (int g = 0; g < groups; g++) {
            final int vk = keys[g * 5 + 0];
            int rx = keys[g * 5 + 1];
            int ry = keys[g * 5 + 2];
            int rw = keys[g * 5 + 3];
            int rh = keys[g * 5 + 4];

            if (vk <= 0 || rw <= 0 || rh <= 0) continue;

            int ax = rx + loc[0] - FK_PADDING_PX;
            int ay = ry + loc[1] - FK_PADDING_PX;
            int aw = rw + FK_PADDING_PX * 2;
            int ah = rh + FK_PADDING_PX * 2;

            KeyWindow kw = findKeyWindow(vk);
            boolean isNew = false;

            if (kw == null) {
                kw = freeKeyWindow();
                if (kw == null) continue; // já no máximo de botões
                kw.vk = vk;
                isNew = true;
            }

            used[indexOfKeyWindow(kw)] = true;

            if (isNew) {
                createKeyWindow(kw);
            }

            if (!kw.added) {
                kw.x = ax; kw.y = ay; kw.w = aw; kw.h = ah;
                kw.params.x = ax;
                kw.params.y = ay;
                kw.params.width = aw;
                kw.params.height = ah;
                try {
                    windowManager.addView(kw.view, kw.params);
                    kw.added = true;
                } catch (Throwable t) {
                    Log.w(TAG, "addView botão flutuante falhou: " + t.getMessage());
                }
            } else if (ax != kw.x || ay != kw.y || aw != kw.w || ah != kw.h) {
                kw.x = ax; kw.y = ay; kw.w = aw; kw.h = ah;
                kw.params.x = ax;
                kw.params.y = ay;
                kw.params.width = aw;
                kw.params.height = ah;
                try {
                    windowManager.updateViewLayout(kw.view, kw.params);
                } catch (Throwable ignored) {
                }
            }
        }

        // remove janelas de botões que deixaram de existir
        for (int i = 0; i < MAX_FLOATING_KEYS; i++) {
            KeyWindow kw = keyWindows[i];
            if (kw != null && kw.added && !used[i]) {
                removeKeyWindow(kw);
            }
        }
    }

    private KeyWindow findKeyWindow(int vk) {
        for (KeyWindow kw : keyWindows) {
            if (kw != null && kw.view != null && kw.vk == vk) return kw;
        }
        return null;
    }

    private KeyWindow freeKeyWindow() {
        for (int i = 0; i < MAX_FLOATING_KEYS; i++) {
            KeyWindow kw = keyWindows[i];
            if (kw == null) {
                kw = new KeyWindow();
                keyWindows[i] = kw;
            }
            if (!kw.added) return kw;
        }
        return null;
    }

    private int indexOfKeyWindow(KeyWindow kw) {
        for (int i = 0; i < MAX_FLOATING_KEYS; i++) {
            if (keyWindows[i] == kw) return i;
        }
        return -1;
    }

    private void createKeyWindow(KeyWindow kw) {
        View v = new View(this);
        v.setBackgroundColor(Color.TRANSPARENT);

        int flag = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams p = new WindowManager.LayoutParams(
                kw.w > 0 ? kw.w : 100,
                kw.h > 0 ? kw.h : 60,
                flag,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT
        );
        p.gravity = Gravity.TOP | Gravity.START;

        applyCutoutMode(p);

        /* STREAM MODE: key windows tambem somem da captura. */
        if (lastCaptureBypass) {
            p.flags |= WindowManager.LayoutParams.FLAG_SECURE;
        }

        p.x = kw.x;
        p.y = kw.y;
        p.width = kw.w;
        p.height = kw.h;

        v.setOnTouchListener((view, event) -> {
            final int vk = kw.vk;
            int action = event.getActionMasked();

            switch (action) {
                case MotionEvent.ACTION_DOWN: {
                    kw.view = view;
                    kw.lastRawX = event.getRawX();
                    kw.lastRawY = event.getRawY();
                    kw.downRawX = event.getRawX();
                    kw.downRawY = event.getRawY();
                    kw.dragging = false;
                    try { nativeFloatingKeyTouch(vk, true); } catch (Throwable ignored) {}
                    return true;
                }

                case MotionEvent.ACTION_MOVE: {
                    float totalDx = event.getRawX() - kw.downRawX;
                    float totalDy = event.getRawY() - kw.downRawY;

                    if (!kw.dragging && Math.hypot(totalDx, totalDy) > touchSlopPx) {
                        kw.dragging = true;
                        // virou arrasto: cancela o toggle/hold do toque
                        try { nativeFloatingKeyDrag(vk); } catch (Throwable ignored) {}
                    }

                    if (kw.dragging) {
                        float dx = event.getRawX() - kw.lastRawX;
                        float dy = event.getRawY() - kw.lastRawY;
                        if (dx != 0f || dy != 0f) {
                            try { nativeFloatingKeyMove(vk, dx, dy); } catch (Throwable ignored) {}
                        }
                    }

                    kw.lastRawX = event.getRawX();
                    kw.lastRawY = event.getRawY();
                    return true;
                }

                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL: {
                    try { nativeFloatingKeyTouch(vk, false); } catch (Throwable ignored) {}
                    kw.dragging = false;
                    return true;
                }
            }

            return false;
        });

        kw.view = v;
        kw.params = p;
    }

    private void removeKeyWindow(KeyWindow kw) {
        if (kw == null || !kw.added) return;
        try {
            windowManager.removeView(kw.view);
        } catch (Throwable ignored) {
        }
        kw.added = false;
        kw.view = null;
    }

    private void removeAllKeyWindows() {
        for (int i = 0; i < MAX_FLOATING_KEYS; i++) {
            if (keyWindows[i] != null) {
                removeKeyWindow(keyWindows[i]);
                keyWindows[i] = null;
            }
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "Surface created");
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "Surface changed: " + width + "x" + height);
        nativeResize(width, height);
        if (renderThread == null || !renderThread.isAlive()) {
            renderThread = new Thread(() -> nativeStartPanel(holder.getSurface()));
            renderThread.start();
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "Surface destroyed");
        nativeStopPanel();
        if (renderThread != null) {
            try { renderThread.join(2000); } catch (InterruptedException ignored) {}
            renderThread = null;
        }
    }

    private Notification buildNotification() {
        return new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Storm Panel")
                .setContentText("Overlay ativo")
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .setOngoing(true)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Storm Overlay", NotificationManager.IMPORTANCE_LOW);
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }

    public native void nativeStartPanel(Surface surface);
    public native void nativeResize(int width, int height);
    public native void nativeStopPanel();
    public native void nativeOnTouch(int action, float x, float y, int pointerId);
    public native int[] nativeGetPanelBounds();

    // STREAM MODE (Task 12): o painel (ImGui) responde se o CaptureBypass
    // esta ligado; o Java aplica/remove FLAG_SECURE na janela de render.
    public native boolean nativeShouldCaptureBypass();

    // BOTÕES FLUTUANTES (implementados em Client/main.cpp)
    public native int[] nativeGetFloatingKeys();
    public native void nativeFloatingKeyTouch(int vk, boolean down);
    public native void nativeFloatingKeyDrag(int vk);
    public native void nativeFloatingKeyMove(int vk, float dx, float dy);
}