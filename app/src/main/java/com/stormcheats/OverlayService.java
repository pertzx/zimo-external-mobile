package com.stormcheats;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;

public class OverlayService extends Service implements SurfaceHolder.Callback {
    private static final String TAG = "StormOverlay";
    private static final String CHANNEL_ID = "storm_overlay_channel";
    private WindowManager windowManager;
    private FrameLayout overlayView;
    private SurfaceView surfaceView;
    private Thread renderThread;

    static {
        try {
            System.loadLibrary("panel");
            Log.i(TAG, "libpanel.so carregada");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Falha ao carregar libpanel.so: " + e.getMessage());
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        createOverlay();
    }

    private void createOverlay() {
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ?
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY :
                        WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                        WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 0;
        params.y = 0;

        overlayView = new FrameLayout(this);
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        surfaceView.getHolder().addCallback(this);

        overlayView.addView(surfaceView);
        windowManager.addView(overlayView, params);

        // Input forwarding
        overlayView.setOnTouchListener((v, event) -> {
            int action = event.getActionMasked();
            int pointerCount = event.getPointerCount();
            for (int i = 0; i < pointerCount; i++) {
                nativeOnTouch(event.getActionMasked(), event.getX(i), event.getY(i), event.getPointerId(i));
            }
            return true; // consume all touch
        });
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Storm Panel")
                .setContentText("Overlay ativo")
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .setOngoing(true)
                .build();
        startForeground(1, notification);
        return START_STICKY;
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
            renderThread = new Thread(() -> {
                nativeStartPanel(holder.getSurface());
            });
            renderThread.start();
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "Surface destroyed");
        nativeStopPanel();
        if (renderThread != null) {
            try {
                renderThread.join(2000);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            renderThread = null;
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        nativeStopPanel();
        if (overlayView != null) {
            windowManager.removeView(overlayView);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Storm Overlay",
                    NotificationManager.IMPORTANCE_LOW
            );
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    // JNI
    public native void nativeStartPanel(Surface surface);
    public native void nativeResize(int width, int height);
    public native void nativeStopPanel();
    public native void nativeOnTouch(int action, float x, float y, int pointerId);
}