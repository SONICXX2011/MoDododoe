package com.myapp.modmenu

import android.app.Activity
import android.os.Bundle
import android.widget.TextView
import android.view.Gravity

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        System.loadLibrary("mymod")

        val tv = TextView(this)
        tv.text = "MyModMenu Loaded\n\nCheck logcat:\nadb logcat -s MyMod"
        tv.gravity = Gravity.CENTER
        setContentView(tv)
    }
}
