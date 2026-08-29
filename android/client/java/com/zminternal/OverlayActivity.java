package com.zminternal;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.WindowManager;

public class OverlayActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Load the native library
        System.loadLibrary("zmclient");

        // Make the activity transparent and non-interactive by default
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
        );

        // Set the layout (would normally be a SurfaceView for rendering)
        setContentView(R.layout.activity_overlay);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // Forward touch events to native code
        // In a real implementation, we would have a JNI method to handle this
        // return nativeOnTouch(event);
        return super.onTouchEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        // Decide whether to consume the touch or pass it through
        // In a real implementation, we would check if touch is within UI bounds
        // For now, we'll pass all touches through to the game
        return super.dispatchTouchEvent(ev);
    }
}