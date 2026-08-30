
package com.stormcheats;

import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.IBinder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.WindowManager.LayoutParams;

public class OverlayService extends Service {
    private WindowManager windowManager;
    private SurfaceView surfaceView;

    @Override
    public void onCreate() {
        super.onCreate();

        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);

        // Criar SurfaceView transparente para renderização OpenGL
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);

        // Layout params: overlay transparente, full screen
        LayoutParams params = new LayoutParams(
            LayoutParams.MATCH_PARENT,
            LayoutParams.MATCH_PARENT,
            LayoutParams.TYPE_APPLICATION_OVERLAY,
            LayoutParams.FLAG_NOT_FOCUSABLE 
                | LayoutParams.FLAG_NOT_TOUCH_MODAL
                | LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        );

        windowManager.addView(surfaceView, params);

        // Iniciar native panel (C++)
        surfaceView.getHolder().addCallback(new android.view.SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(android.view.SurfaceHolder holder) {
                nativeStartPanel(holder.getSurface());
            }
            @Override
            public void surfaceChanged(android.view.SurfaceHolder holder, int format, int w, int h) {
                nativeResize(w, h);
            }
            @Override
            public void surfaceDestroyed(android.view.SurfaceHolder holder) {
                nativeStopPanel();
            }
        });
    }

    @Override
    public void onDestroy() {
        nativeStopPanel();
        if (surfaceView != null) {
            windowManager.removeView(surfaceView);
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // Native methods
    private native void nativeStartPanel(android.view.Surface surface);
    private native void nativeResize(int width, int height);
    private native void nativeStopPanel();

    static {
        System.loadLibrary("panel");
    }
}
