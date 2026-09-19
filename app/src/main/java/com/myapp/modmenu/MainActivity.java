package com.myapp.modmenu;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.view.Gravity;
import android.graphics.Color;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        System.loadLibrary("mymod");

        TextView tv = new TextView(this);
        tv.setText("MyModMenu Loaded\n\nLaunch LAC to activate.");
        tv.setGravity(Gravity.CENTER);
        tv.setTextColor(Color.WHITE);
        tv.setBackgroundColor(Color.BLACK);

        setContentView(tv);
    }
}
