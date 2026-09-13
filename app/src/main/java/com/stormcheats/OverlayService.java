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

    private final Handler panelTracker = new Handler(Looper.getMainLooper());
    private final Runnable trackPanelRunnable = new Runnable() {
        @Override
        public void run() {
            // CORRECAO: se a tela girou (retrato -> paisagem no FreeFire),
            // a janela de render acompanha NA HORA. Sem isso a janela fica
            // presa no tamanho captado quando o servico subiu e o overlay
            // aparece cortado na metade da tela.
            syncScreenSize();
            syncPanelBoundsFromNative();
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
        try { if (surfaceView != null) windowManager.removeView(surfaceView); } catch (Exception ignored) {}
        try { if (touchView != null) windowManager.removeView(touchView); } catch (Exception ignored) {}
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    /**
     * Retorna o tamanho REAL do display (incluindo a area do notch/cutout e a
     * barra de status). O MATCH_PARENT em janelas TYPE_APPLICATION_OVERLAY para
     * ANTES da barra de status / cutout em muitos aparelhos, e era isso que
     * deixava o ESP deslocado em relacao ao FreeFire (que renderiza na tela
     * fisica inteira).
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
     * a janela de render quando mudou (rotacao, dobraveis, etc). Roda a cada
     * 16ms pelo tracker, entao a virada para paisagem no jogo aplica em ~1 frame.
     * Retorna true se o tamanho mudou.
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

    /**
     * Aplica o modo de cutout que deixa a janela entrar na area do
     * notch/barra de status. minSdk e 28 (Android P), entao o campo
     * layoutInDisplayCutoutMode sempre existe no aparelho.
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

        // overlay cobre a area do notch/cutout igual ao jogo
        applyCutoutMode(surfaceParams);
        // tamanho inicial = tela fisica inteira
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

        // se o painel estiver perto do topo, tambem precisa entrar no cutout
        applyCutoutMode(touchParams);

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
}