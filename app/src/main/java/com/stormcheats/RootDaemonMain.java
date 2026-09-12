package com.stormcheats;

import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public final class RootDaemonMain {

    private static final String TAG =
            "StormRootDaemon";

    private RootDaemonMain() {
    }

    public static void main(
            String[] args
    ) {

        String libraryPath =
                args.length > 0
                        ? args[0]
                        : null;

        String pidPath =
                args.length > 1
                        ? args[1]
                        : "/data/local/tmp/storm_daemon.pid";

        if (
                libraryPath == null ||
                libraryPath.isEmpty()
        ) {

            Log.e(
                    TAG,
                    "Caminho da libdaemon.so nao informado"
            );

            return;
        }

        try {

            int pid =
                    android.os.Process.myPid();

            Log.i(
                    TAG,
                    "RootDaemonMain PID=" +
                            pid
            );

            Log.i(
                    TAG,
                    "Carregando: " +
                            libraryPath
            );

            System.load(
                    libraryPath
            );

            writePid(
                    pidPath,
                    pid
            );

            Log.i(
                    TAG,
                    "libdaemon.so carregada"
            );

            Log.i(
                    TAG,
                    "Chamando nativeRunDaemon()"
            );

            nativeRunDaemon();

            Log.i(
                    TAG,
                    "nativeRunDaemon() terminou"
            );

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Falha no RootDaemonMain",
                    e
            );

        } finally {

            try {

                new File(
                        pidPath
                ).delete();

            } catch (Throwable ignored) {
            }
        }
    }

    private static void writePid(
            String path,
            int pid
    ) {

        try {

            FileOutputStream out =
                    new FileOutputStream(
                            path,
                            false
                    );

            out.write(
                    String.valueOf(pid)
                            .getBytes(
                                    StandardCharsets.US_ASCII
                            )
            );

            out.flush();
            out.close();

            Log.i(
                    TAG,
                    "PID salvo em " +
                            path
            );

        } catch (Throwable e) {

            Log.e(
                    TAG,
                    "Falha salvando PID",
                    e
            );
        }
    }

    private static native void
    nativeRunDaemon();
}