package com.myapp.modmenu;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.view.Gravity;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        System.loadLibrary("mymod");

        TextView tv = new TextView(this);
        tv.setText("MyModMenu Loaded\n\nCheck logcat:\nadb logcat -s MyMod");
        tv.setGravity(Gravity.CENTER);
        setContentView(tv);
    }
}
