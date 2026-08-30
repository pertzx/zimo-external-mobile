package com.stormcheats;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Toast;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Solicitar permissão de overlay
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (!Settings.canDrawOverlays(this)) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, 1);
                return;
            }
        }

        startServices();
        finish();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == 1) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                if (Settings.canDrawOverlays(this)) {
                    startServices();
                } else {
                    Toast.makeText(this, "Permissão de overlay necessária", Toast.LENGTH_LONG).show();
                }
            }
        }
        finish();
    }

    private void startServices() {
        // Iniciar daemon
        startService(new Intent(this, DaemonService.class));

        // Iniciar overlay (painel)
        startService(new Intent(this, OverlayService.class));
    }
}