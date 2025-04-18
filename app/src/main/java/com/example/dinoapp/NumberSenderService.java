package com.example.dinoapp;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

public class NumberSenderService extends Service {

    static {
        // Ensure the native library is loaded.
        System.loadLibrary("native-lib");
    }

    // Declare the native function (same signature as in MainActivity).
    public native void sendNumbersToServer(int startNumber);

    private Thread workerThread;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Create a new thread to run the native function:
        workerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                // Start sending numbers beginning at 10.
                sendNumbersToServer(10);
            }
        });
        workerThread.start();

        // Return START_STICKY so the system will recreate the service if it's killed.
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        // Stop the worker thread if it's still running.
        if (workerThread != null && workerThread.isAlive()) {
            workerThread.interrupt();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        // We don't provide binding, so return null.
        return null;
    }
}
