package com.example.dinoapp;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.os.Bundle;

public class MainActivity extends AppCompatActivity {

    // If you need to call native methods from the Activity (not required here),
    // you can still load the library. Otherwise, it can remain only in the Service.
    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Instead of calling the native method directly here,
        // we start the background service to do the work.
        Intent serviceIntent = new Intent(this, NumberSenderService.class);
        startService(serviceIntent);
    }
}
