package com.example.dinoapp;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.app.ActivityCompat;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;

public class MainActivity extends AppCompatActivity {

    private static final int FOREGROUND_SERVICE_REQUEST_CODE = 100;

    // If you need to call native methods from the Activity (not required here),
    // you can still load the library. Otherwise, it can remain only in the Service.
    static {
        System.loadLibrary("native-lib");
    }

    private Button connectButton;
    private boolean isConnected = false;
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Initialize SharedPreferences and restore connection state
        prefs = getSharedPreferences("dinoapp_prefs", MODE_PRIVATE);
        isConnected = prefs.getBoolean("connected", false);

        // Initialize the connect button
        connectButton = findViewById(R.id.connect_button);
        // Update button text and state based on restored state
        updateButtonAppearance();

        connectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                // Toggle connection state immediately
                isConnected = !isConnected;

                // Update button appearance right away for immediate visual feedback
                updateButtonAppearance();

                // Then handle the actual connection logic
                if (isConnected) {
                    // Start connection
                    startConnectionService();
                    // Save state
                    prefs.edit().putBoolean("connected", true).apply();
                } else {
                    // Stop the connection service
                    Toast.makeText(MainActivity.this, "Disconnecting from server...", Toast.LENGTH_SHORT).show();
                    Intent serviceIntent = new Intent(MainActivity.this, NumberSenderService.class);
                    stopService(serviceIntent);
                    // Save disconnected state
                    prefs.edit().putBoolean("connected", false).apply();
                }
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Refresh connection state in case service was started
        isConnected = prefs.getBoolean("connected", false);
        updateButtonAppearance();
    }

    private void updateButtonAppearance() {
        connectButton.setText(isConnected ? "Disconnect" : "Connect");
        connectButton.setActivated(isConnected);
    }

    private void startConnectionService() {
        // Check if we have the necessary permission for foreground service
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this,
                    Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {

                ActivityCompat.requestPermissions(this,
                        new String[] { Manifest.permission.POST_NOTIFICATIONS },
                        FOREGROUND_SERVICE_REQUEST_CODE);
                return; // Will continue in onRequestPermissionsResult
            }
        }

        // Permission granted or not needed, start the service
        Toast.makeText(MainActivity.this, "Connecting to server...", Toast.LENGTH_SHORT).show();

        Intent serviceIntent = new Intent(this, NumberSenderService.class);

        // For Android 12+, must use one of these methods to start a foreground service
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            startForegroundService(serviceIntent);
        } else {
            startService(serviceIntent);
        }

        // We've already toggled the button state in onClick, so we don't need to do it
        // here
        // Save connected state
        prefs.edit().putBoolean("connected", true).apply();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (requestCode == FOREGROUND_SERVICE_REQUEST_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                // Permission was granted, start the service
                startConnectionService();
            } else {
                // Permission denied
                Toast.makeText(this, "Notification permission is required for stable connection",
                        Toast.LENGTH_LONG).show();
                // Reset the button since we couldn't connect
                isConnected = false;
                updateButtonAppearance();
                prefs.edit().putBoolean("connected", false).apply();
            }
        }
    }
}
