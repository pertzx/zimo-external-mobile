package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

public class OverlayService extends Service implements SurfaceHolder.Callback {

    private static final String TAG = "StormOverlay";
    private static final String CHANNEL_ID = "storm_overlay_channel";

    private WindowManager windowManager;

    private static final int TOUCH_PADDING_PX = 8; // folga na borda da janela de toque
    private volatile boolean isTouching = false;
    private float touchDownX = 0f;
private float touchDownY = 0f;
private boolean touchMoved = false;
private static final float TOUCH_SLOP_DP = 8f;  // 8dp de folga
private float touchSlopPx;

    // Janela A — renderização (fullscreen, NOT_TOUCHABLE)
    private SurfaceView surfaceView;
    private WindowManager.LayoutParams surfaceParams;

    // Janela B — input (segue o painel do ImGui)
    private View touchView;
    private WindowManager.LayoutParams touchParams;

    private Thread renderThread;

    // Bounds atuais da Janela B, em pixels absolutos de TELA
    private int panelX = 0;
    private int panelY = 0;
    private int panelW = 300;
    private int panelH = 500;

    // Polling que sincroniza a Janela B com o painel do ImGui
    private final Handler panelTracker = new Handler(Looper.getMainLooper());
    private final Runnable trackPanelRunnable = new Runnable() {
    @Override
    public void run() {
        syncPanelBoundsFromNative();
        panelTracker.postDelayed(this, 16); // 60 Hz
    }
};

    static {
        try {
            System.loadLibrary("panel");
            Log.i(TAG, "libpanel.so carregada");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Falha ao carregar libpanel.so: " + e.getMessage());
        }
    }

    // ============================================================
    //  CICLO DE VIDA
    // ============================================================
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
    public void onDestroy() {
        super.onDestroy();
        panelTracker.removeCallbacks(trackPanelRunnable);
        nativeStopPanel();
        try { if (surfaceView != null) windowManager.removeView(surfaceView); } catch (Exception ignored) {}
        try { if (touchView != null) windowManager.removeView(touchView); } catch (Exception ignored) {}
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // ============================================================
    //  CRIAÇÃO DAS JANELAS
    // ============================================================
    private void createOverlay() {
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        touchSlopPx = TOUCH_SLOP_DP * getResources().getDisplayMetrics().density;
        createRenderWindow();
        createTouchWindow();
    }

    // -------- JANELA A: renderização fullscreen, NOT_TOUCHABLE --------
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
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS  // << cobre status bar
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT
        );
        surfaceParams.gravity = Gravity.TOP | Gravity.START;
        surfaceParams.x = 0;
        surfaceParams.y = 0;

        windowManager.addView(surfaceView, surfaceParams);
    }

    // -------- JANELA B: input, cobre o painel --------
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

        // Reseta o filtro de movimento
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

        // ===== FILTRO: suprime MOVE até o dedo andar o suficiente =====
        if (pointerAction == MotionEvent.ACTION_MOVE && i == 0) {
            if (!touchMoved) {
                float dx = event.getRawX(0) - touchDownX;
                float dy = event.getRawY(0) - touchDownY;
                if (Math.hypot(dx, dy) > touchSlopPx) {
                    touchMoved = true;
                    // Manda o primeiro MOVE já na posição atual pra ImGui
                    // "pegar" o movimento a partir daqui
                    nativeOnTouch(MotionEvent.ACTION_MOVE, sx, sy, event.getPointerId(i));
                }
                // Se ainda não passou o slop, NÃO manda MOVE (ImGui vê como clique)
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
    // Manda um MOVE "fantasma" depois do UP pra ImGui atualizar o hover
    // e destravar popups que dependem disso
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

    // ============================================================
    //  SINCRONIZAÇÃO COM O PAINEL DO IMGUI (polling)
    // ============================================================
    private void syncPanelBoundsFromNative() {
    if (touchView == null) return;
    if (isTouching) return; // NÃO move a janela durante um gesto ativo

    int[] b;
    try {
        b = nativeGetPanelBounds();
    } catch (Throwable t) {
        return;
    }
    if (b == null || b.length < 4) return;
    if (b[2] <= 0 || b[3] <= 0) return; // painel escondido

    int[] loc = new int[2];
    surfaceView.getLocationOnScreen(loc);

    int absX = b[0] + loc[0];
    int absY = b[1] + loc[1];
    int absW = b[2];
    int absH = b[3];

    // ===== Aplica padding (folga) em cada lado =====
    int newX = absX - TOUCH_PADDING_PX;
    int newY = absY - TOUCH_PADDING_PX;
    int newW = absW + TOUCH_PADDING_PX * 2;
    int newH = absH + TOUCH_PADDING_PX * 2;

    if (newX == panelX && newY == panelY && newW == panelW && newH == panelH) {
        return; // nada mudou
    }

    panelX = newX;
    panelY = newY;
    panelW = newW;
    panelH = newH;

    touchParams.x = panelX;
    touchParams.y = panelY;
    touchParams.width = panelW;
    touchParams.height = panelH;
    try {
        windowManager.updateViewLayout(touchView, touchParams);
    } catch (Exception ignored) {}
}

    // ============================================================
    //  SurfaceHolder.Callback
    // ============================================================
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

    // ============================================================
    //  Notificação
    // ============================================================
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

    // ============================================================
    //  JNI
    // ============================================================
    public native void nativeStartPanel(Surface surface);
    public native void nativeResize(int width, int height);
    public native void nativeStopPanel();
    public native void nativeOnTouch(int action, float x, float y, int pointerId);

    // NOVA — retorna [x, y, w, h] do painel ImGui, em pixels da surface.
    public native int[] nativeGetPanelBounds();
}