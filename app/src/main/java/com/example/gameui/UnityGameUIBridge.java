package com.example.gameui;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
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
        if (sInstance == null) sInstance = new UnityGameUIBridge();
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
            @Override public void run() { addView(activity); }
        });
    }

    public void hide() { Log.i(TAG, "hide() called"); }

    public void setGameState(int menu, boolean networkActive) {
        updateStatus("menu=" + menu + " net=" + networkActive);
    }

    public void setPlayerName(String name) {}
    public void showJoinNotification() { log("Join notification"); }
    public void onCharacterEvent() {}
    public void onBackMenuEvent() {}
    public void onExitEvent() { log("Network exit"); }
    public void showWelcomeOnce(Activity activity) {}

    public void log(final String msg) {
        Log.i(TAG, msg);
        if (mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mLogText == null) return;
                String cur = mLogText.getText().toString();
                String next = cur + msg + "\n";
                if (next.length() > 40000) next = next.substring(next.length() - 40000);
                mLogText.setText(next);
            }
        });
    }

    private void copyLogsToClipboard() {
        try {
            if (mLogText == null || mCurrentActivity == null) return;
            String text = mLogText.getText().toString();
            ClipboardManager cm = (ClipboardManager)
                    mCurrentActivity.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null) {
                cm.setPrimaryClip(ClipData.newPlainText("LACMod Logs", text));
                Toast.makeText(mCurrentActivity, "Logs copied!", Toast.LENGTH_SHORT).show();
                Log.i(TAG, "Logs copied (" + text.length() + " chars)");
            }
        } catch (Exception e) {
            Log.e(TAG, "copyLogs failed", e);
        }
    }

    private void clearLogs() {
        if (mLogText == null || mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mLogText != null) mLogText.setText("");
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
                950, ViewGroup.LayoutParams.WRAP_CONTENT);
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

        // Row 1
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
        joinBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Join clicked ---");
                updateStatus("Joining...");
                nativeRequestStartGame();
            }
        });
        row1.addView(joinBtn);

        Button dumpBtn = new Button(activity);
        dumpBtn.setText("Dump");
        dumpBtn.setTextColor(Color.WHITE);
        dumpBtn.setTextSize(12);
        GradientDrawable dbg = new GradientDrawable();
        dbg.setColor(0xFF2196F3);
        dbg.setCornerRadius(12f);
        dumpBtn.setBackground(dbg);
        dumpBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Dump ---");
                nativeDumpStartClient();
            }
        });
        row1.addView(dumpBtn);
        panel.addView(row1);

        // Row 2
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
        disableBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Disable Buttons ---");
                nativeDisableButtons();
            }
        });
        row2.addView(disableBtn);

        Button clearBtn = new Button(activity);
        clearBtn.setText("Clear");
        clearBtn.setTextColor(Color.WHITE);
        clearBtn.setTextSize(12);
        GradientDrawable clbg = new GradientDrawable();
        clbg.setColor(0xFFFF9800);
        clbg.setCornerRadius(12f);
        clearBtn.setBackground(clbg);
        clearBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { clearLogs(); }
        });
        row2.addView(clearBtn);
        panel.addView(row2);

        // Row 3
        LinearLayout row3 = new LinearLayout(activity);
        row3.setOrientation(LinearLayout.HORIZONTAL);

        Button copyBtn = new Button(activity);
        copyBtn.setText("Copy Logs");
        copyBtn.setTextColor(Color.WHITE);
        copyBtn.setTextSize(12);
        GradientDrawable cpybg = new GradientDrawable();
        cpybg.setColor(0xFF00BCD4);
        cpybg.setCornerRadius(12f);
        copyBtn.setBackground(cpybg);
        copyBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { copyLogsToClipboard(); }
        });
        row3.addView(copyBtn);

        Button closeBtn = new Button(activity);
        closeBtn.setText("Close");
        closeBtn.setTextColor(Color.WHITE);
        closeBtn.setTextSize(12);
        GradientDrawable cbg = new GradientDrawable();
        cbg.setColor(0xFFE53935);
        cbg.setCornerRadius(12f);
        closeBtn.setBackground(cbg);
        closeBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { removeView(); }
        });
        row3.addView(closeBtn);
        panel.addView(row3);

        ScrollView scroll = new ScrollView(activity);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 600);
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
            @Override public void run() {
                if (mStatusText != null) mStatusText.setText(s);
            }
        });
    }
}