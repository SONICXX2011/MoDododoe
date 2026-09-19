package com.example.gameui;

import android.app.Activity;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;

public class UnityGameUIBridge {

    private static final String TAG = "LACMod-UI";

    public static native void nativeInit(UnityGameUIBridge self);
    public static native void nativeRequestStartGame();
    public static native void nativeRequestExit();
    public static native void nativeDumpStartClient();
    public static native void nativeDisableButtons();

    private static UnityGameUIBridge sInstance;

    public static synchronized UnityGameUIBridge getInstance() {
        if (sInstance == null) {
            sInstance = new UnityGameUIBridge();
        }
        return sInstance;
    }

    private View mRootView = null;
    private Activity mCurrentActivity = null;
    private TextView mStatusText = null;
    private TextView mLogText = null;

    private UnityGameUIBridge() {
        Log.i(TAG, "Bridge created");
    }

    public void show(final Activity activity) {
        if (activity == null) return;
        mCurrentActivity = activity;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                addView(activity);
            }
        });
    }

    public void hide() {
        Log.i(TAG, "hide() called");
    }

    public void setGameState(int menu, boolean networkActive) {
        updateStatus("menu=" + menu + " net=" + networkActive);
    }

    public void setPlayerName(String name) {
    }

    public void showJoinNotification() {
        log("Join notification");
    }

    public void onCharacterEvent() {
    }

    public void onBackMenuEvent() {
    }

    public void onExitEvent() {
        log("Network exit");
    }

    public void showWelcomeOnce(Activity activity) {
    }

    public void log(final String msg) {
        Log.i(TAG, msg);
        if (mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mLogText == null) return;
                String cur = mLogText.getText().toString();
                String next = cur + msg + "\n";
                if (next.length() > 4000) {
                    next = next.substring(next.length() - 4000);
                }
                mLogText.setText(next);
            }
        });
    }

    private void addView(Activity activity) {
        if (mRootView != null) return;

        ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();

        FrameLayout container = new FrameLayout(activity);
        container.setLayoutParams(new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        container.setClickable(false);

        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(30, 30, 30, 30);

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xEE1E1E1E);
        bg.setCornerRadius(24f);
        bg.setStroke(3, 0xFF00E5FF);
        panel.setBackground(bg);

        FrameLayout.LayoutParams panelLp = new FrameLayout.LayoutParams(
            900,
            ViewGroup.LayoutParams.WRAP_CONTENT);
        panelLp.gravity = Gravity.TOP | Gravity.START;
        panelLp.leftMargin = 40;
        panelLp.topMargin = 150;
        panel.setLayoutParams(panelLp);
        panel.setElevation(20f);

        TextView title = new TextView(activity);
        title.setText("Mod Menu");
        title.setTextColor(0xFF00E5FF);
        title.setTextSize(18);
        title.setPadding(0, 0, 0, 20);
        panel.addView(title);

        mStatusText = new TextView(activity);
        mStatusText.setText("idle");
        mStatusText.setTextColor(0xFFAAAAAA);
        mStatusText.setTextSize(11);
        mStatusText.setPadding(0, 0, 0, 20);
        panel.addView(mStatusText);

        LinearLayout row1 = new LinearLayout(activity);
        row1.setOrientation(LinearLayout.HORIZONTAL);

        Button joinBtn = new Button(activity);
        joinBtn.setText("Join Server");
        joinBtn.setTextColor(Color.WHITE);
        joinBtn.setTextSize(12);
        GradientDrawable jbg = new GradientDrawable();
        jbg.setColor(0xFF00C853);
        jbg.setCornerRadius(12f);
        joinBtn.setBackground(jbg);
        joinBtn.setPadding(30, 20, 30, 20);
        joinBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                log("--- Join clicked ---");
                updateStatus("Joining...");
                nativeRequestStartGame();
            }
        });
        row1.addView(joinBtn);

        Button dumpBtn = new Button(activity);
        dumpBtn.setText("Dump StartClient");
        dumpBtn.setTextColor(Color.WHITE);
        dumpBtn.setTextSize(12);
        GradientDrawable dbg = new GradientDrawable();
        dbg.setColor(0xFF2196F3);
        dbg.setCornerRadius(12f);
        dumpBtn.setBackground(dbg);
        dumpBtn.setPadding(30, 20, 30, 20);
        dumpBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                log("--- Dump StartClient ---");
                nativeDumpStartClient();
            }
        });
        row1.addView(dumpBtn);

        panel.addView(row1);

        LinearLayout row2 = new LinearLayout(activity);
        row2.setOrientation(LinearLayout.HORIZONTAL);

        Button disableBtn = new Button(activity);
        disableBtn.setText("Disable Buttons");
        disableBtn.setTextColor(Color.WHITE);
        disableBtn.setTextSize(12);
        GradientDrawable disbg = new GradientDrawable();
        disbg.setColor(0xFF9C27B0);
        disbg.setCornerRadius(12f);
        disableBtn.setBackground(disbg);
        disableBtn.setPadding(30, 20, 30, 20);
        disableBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                log("--- Disable Buttons ---");
                nativeDisableButtons();
            }
        });
        row2.addView(disableBtn);

        Button closeBtn = new Button(activity);
        closeBtn.setText("Close");
        closeBtn.setTextColor(Color.WHITE);
        closeBtn.setTextSize(12);
        GradientDrawable cbg = new GradientDrawable();
        cbg.setColor(0xFFE53935);
        cbg.setCornerRadius(12f);
        closeBtn.setBackground(cbg);
        closeBtn.setPadding(30, 20, 30, 20);
        closeBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                removeView();
            }
        });
        row2.addView(closeBtn);

        panel.addView(row2);

        ScrollView scroll = new ScrollView(activity);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            600);
        scrollLp.topMargin = 20;
        scroll.setLayoutParams(scrollLp);
        scroll.setBackgroundColor(0xFF000000);

        mLogText = new TextView(activity);
        mLogText.setText("Waiting for native logs...\n");
        mLogText.setTextColor(0xFF00FF00);
        mLogText.setTextSize(10);
        mLogText.setPadding(15, 15, 15, 15);
        mLogText.setTypeface(Typeface.MONOSPACE);
        mLogText.setTextIsSelectable(true);
        scroll.addView(mLogText);
        panel.addView(scroll);

        container.addView(panel);
        decor.addView(container);
        mRootView = container;

        Log.i(TAG, "Mod menu shown");
    }

    private void removeView() {
        if (mRootView == null) return;
        ViewGroup parent = (ViewGroup) mRootView.getParent();
        if (parent != null) parent.removeView(mRootView);
        mRootView = null;
        mStatusText = null;
        Log.i(TAG, "Mod menu hidden");
    }

    private void updateStatus(final String s) {
        Log.i(TAG, "status: " + s);
        if (mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mStatusText != null) {
                    mStatusText.setText(s);
                }
            }
        });
    }
}