package com.example.dinoapp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import androidx.core.app.NotificationCompat;

public class NumberSenderService extends Service {

    static {
        // Ensure the native library is loaded.
        System.loadLibrary("native-lib");
    }

    private static final String TAG = "NumberSenderService";
    private static final int NOTIFICATION_ID = 1;
    private static final String CHANNEL_ID = "DinoConnectionChannel";

    // Declare the native functions
    public native void sendNumbersToServer(int startNumber);

    public native void stopConnection();

    private Thread workerThread;
    private volatile boolean isRunning = false;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "Service starting connection to server");

        // Start as a foreground service with notification
        startForeground(NOTIFICATION_ID, createNotification("Connected to server"));

        // Only start a new thread if one isn't already running
        if (workerThread == null || !workerThread.isAlive()) {
            isRunning = true;

            // Create a new thread to run the native function:
            workerThread = new Thread(new Runnable() {
                @Override
                public void run() {
                    // Start sending numbers beginning at 10.
                    Log.d(TAG, "Starting to send numbers to server");
                    sendNumbersToServer(10);
                }
            });
            workerThread.start();
        }

        // If service is killed, restart it
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.d(TAG, "Service being destroyed, stopping connection");

        // Signal the native code to stop
        stopConnection();

        // Signal the thread to stop and wait for it to finish
        isRunning = false;
        if (workerThread != null && workerThread.isAlive()) {
            workerThread.interrupt();
            try {
                // Wait for the thread to finish, but with a timeout
                workerThread.join(1000);
            } catch (InterruptedException e) {
                Log.e(TAG, "Interrupted while waiting for worker thread to stop", e);
            }
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        // We don't provide binding, so return null.
        return null;
    }

    private void createNotificationChannel() {
        // Create the notification channel for Android 8.0 and higher
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Server Connection",
                    NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Channel for server connection notification");

            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(channel);
        }
    }

    private Notification createNotification(String contentText) {
        // Create an explicit intent for the main activity
        Intent notificationIntent = new Intent(this, MainActivity.class);
        // Bring existing activity to front, don't create a new one
        notificationIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                notificationIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        // Build the notification
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("Dino App Connection")
                .setContentText(contentText)
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentIntent(pendingIntent)
                .setPriority(NotificationCompat.PRIORITY_LOW);

        return builder.build();
    }

    // Callback from native code to update UI progress
    public void onNativeProgress(int percent) {
        // Create intent and pending intent for tap action
        Intent notificationIntent = new Intent(this, MainActivity.class);
        notificationIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                notificationIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        // Build progress notification
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("Dino App Connection")
                .setContentText("Progress: " + percent + "%")
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentIntent(pendingIntent)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setProgress(100, percent, false);
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        nm.notify(NOTIFICATION_ID, builder.build());
    }
}
