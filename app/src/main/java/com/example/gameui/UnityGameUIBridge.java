package com.example.gameui;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class UnityGameUIBridge {

    private static final String TAG = "LACMod-UI";

    public static native void nativeInit(UnityGameUIBridge self);
    public static native void nativeRequestStartGame();
    public static native void nativeRequestExit();
    public static native void nativeDumpStartClient();
    public static native void nativeDisableButtons();
    public static native void nativeGetObjects();

    private static UnityGameUIBridge sInstance;

    public static synchronized UnityGameUIBridge getInstance() {
        if (sInstance == null) sInstance = new UnityGameUIBridge();
        return sInstance;
    }

    private View mRootView = null;
    private LinearLayout mPanel = null;
    private FrameLayout mFloatingToggle = null;
    private Activity mCurrentActivity = null;
    private TextView mStatusText = null;
    private TextView mLogText = null;
    private boolean mIsOpen = false;

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

    public void hide() {
        if (mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mRootView != null) mRootView.setVisibility(View.GONE);
            }
        });
    }

    public void setGameState(int menu, boolean networkActive) {
        updateStatus("menu=" + menu + " net=" + networkActive);
    }

    public void setPlayerName(String name) {}
    public void showJoinNotification() { log("Join notification"); }
    public void onCharacterEvent() { log(">>> Character event"); }
    public void onBackMenuEvent() { log(">>> BackMenu event"); }
    public void onExitEvent() { log(">>> Exit event"); }
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

    private void attachDragHandler(View handle, View target) {
        handle.setOnTouchListener(new View.OnTouchListener() {
            private float startX, startY, startTouchX, startTouchY;
            private boolean dragging = false;

            @Override public boolean onTouch(View v, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        startX = target.getX();
                        startY = target.getY();
                        startTouchX = event.getRawX();
                        startTouchY = event.getRawY();
                        dragging = false;
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        float dx = event.getRawX() - startTouchX;
                        float dy = event.getRawY() - startTouchY;
                        if (!dragging && (Math.abs(dx) > 10 || Math.abs(dy) > 10))
                            dragging = true;
                        if (dragging) {
                            float newX = startX + dx;
                            float newY = startY + dy;
                            View parent = (View) target.getParent();
                            if (parent != null) {
                                float maxX = parent.getWidth() - target.getWidth();
                                float maxY = parent.getHeight() - target.getHeight();
                                if (newX < 0) newX = 0;
                                if (newY < 0) newY = 0;
                                if (newX > maxX) newX = maxX;
                                if (newY > maxY) newY = maxY;
                            }
                            target.setX(newX);
                            target.setY(newY);
                        }
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        return dragging;
                }
                return false;
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
            }
        } catch (Exception e) {
            Log.e(TAG, "copyLogs failed", e);
        }
    }

    private void clearLogs() {
        if (mLogText == null) return;
        mLogText.setText("");
    }

    private void togglePanel() {
        if (mPanel == null) return;
        if (mIsOpen) {
            mPanel.setVisibility(View.GONE);
            mIsOpen = false;
        } else {
            mPanel.setVisibility(View.VISIBLE);
            mIsOpen = true;
        }
    }

    private void addView(final Activity activity) {
        if (mRootView != null) {
            mRootView.setVisibility(View.VISIBLE);
            return;
        }

        ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();

        FrameLayout container = new FrameLayout(activity);
        container.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        container.setClickable(false);

        mFloatingToggle = new FrameLayout(activity);
        FrameLayout.LayoutParams toggleLp = new FrameLayout.LayoutParams(140, 140);
        toggleLp.gravity = Gravity.TOP | Gravity.START;
        toggleLp.leftMargin = 40;
        toggleLp.topMargin = 200;
        mFloatingToggle.setLayoutParams(toggleLp);

        GradientDrawable toggleBg = new GradientDrawable();
        toggleBg.setShape(GradientDrawable.OVAL);
        toggleBg.setColor(0xEE1E1E1E);
        toggleBg.setStroke(4, 0xFF00E5FF);
        mFloatingToggle.setBackground(toggleBg);
        mFloatingToggle.setElevation(30f);

        TextView toggleTv = new TextView(activity);
        toggleTv.setText("MOD");
        toggleTv.setTextColor(0xFF00E5FF);
        toggleTv.setTextSize(14);
        toggleTv.setTypeface(null, Typeface.BOLD);
        FrameLayout.LayoutParams tvLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        tvLp.gravity = Gravity.CENTER;
        toggleTv.setLayoutParams(tvLp);
        toggleTv.setGravity(Gravity.CENTER);
        mFloatingToggle.addView(toggleTv);

        mFloatingToggle.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { togglePanel(); }
        });
        attachDragHandler(mFloatingToggle, mFloatingToggle);
        container.addView(mFloatingToggle);

        mPanel = new LinearLayout(activity);
        mPanel.setOrientation(LinearLayout.VERTICAL);
        mPanel.setPadding(30, 30, 30, 30);

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xEE1E1E1E);
        bg.setCornerRadius(24f);
        bg.setStroke(3, 0xFF00E5FF);
        mPanel.setBackground(bg);

        FrameLayout.LayoutParams panelLp = new FrameLayout.LayoutParams(
                1000, ViewGroup.LayoutParams.WRAP_CONTENT);
        panelLp.gravity = Gravity.TOP | Gravity.START;
        panelLp.leftMargin = 40;
        panelLp.topMargin = 360;
        mPanel.setLayoutParams(panelLp);
        mPanel.setElevation(20f);

        LinearLayout titleBar = new LinearLayout(activity);
        titleBar.setOrientation(LinearLayout.HORIZONTAL);
        titleBar.setGravity(Gravity.CENTER_VERTICAL);
        titleBar.setPadding(0, 0, 0, 20);

        TextView title = new TextView(activity);
        title.setText("LAC Mod Menu");
        title.setTextColor(0xFF00E5FF);
        title.setTextSize(18);
        title.setTypeface(null, Typeface.BOLD);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        title.setLayoutParams(titleLp);
        titleBar.addView(title);

        TextView minimizeBtn = new TextView(activity);
        minimizeBtn.setText("—");
        minimizeBtn.setTextColor(0xFFFFA726);
        minimizeBtn.setTextSize(20);
        minimizeBtn.setPadding(20, 0, 20, 0);
        minimizeBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { togglePanel(); }
        });
        titleBar.addView(minimizeBtn);
        mPanel.addView(titleBar);
        attachDragHandler(titleBar, mPanel);

        mStatusText = new TextView(activity);
        mStatusText.setText("idle");
        mStatusText.setTextColor(0xFFAAAAAA);
        mStatusText.setTextSize(11);
        mStatusText.setPadding(0, 0, 0, 20);
        mPanel.addView(mStatusText);

        // Row 1: Get Objects + Disable
        LinearLayout row1 = new LinearLayout(activity);
        row1.setOrientation(LinearLayout.HORIZONTAL);

        Button getObjBtn = new Button(activity);
        getObjBtn.setText("📦 Get Objects");
        getObjBtn.setTextColor(Color.WHITE);
        getObjBtn.setTextSize(13);
        GradientDrawable gobg = new GradientDrawable();
        gobg.setColor(0xFF2196F3);
        gobg.setCornerRadius(12f);
        getObjBtn.setBackground(gobg);
        getObjBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Get Objects clicked ---");
                nativeGetObjects();
            }
        });
        row1.addView(getObjBtn);

        Button disableBtn = new Button(activity);
        disableBtn.setText("🚫 Disable");
        disableBtn.setTextColor(Color.WHITE);
        disableBtn.setTextSize(13);
        GradientDrawable disbg = new GradientDrawable();
        disbg.setColor(0xFF9C27B0);
        disbg.setCornerRadius(12f);
        disableBtn.setBackground(disbg);
        disableBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Disable clicked ---");
                nativeDisableButtons();
            }
        });
        row1.addView(disableBtn);
        mPanel.addView(row1);

        // Row 2: Join Server + Copy Logs
        LinearLayout row2 = new LinearLayout(activity);
        row2.setOrientation(LinearLayout.HORIZONTAL);

        Button joinBtn = new Button(activity);
        joinBtn.setText("🔌 Join Server");
        joinBtn.setTextColor(Color.WHITE);
        joinBtn.setTextSize(13);
        GradientDrawable jbg = new GradientDrawable();
        jbg.setColor(0xFF00C853);
        jbg.setCornerRadius(12f);
        joinBtn.setBackground(jbg);
        joinBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                log("--- Join clicked ---");
                nativeRequestStartGame();
            }
        });
        row2.addView(joinBtn);

        Button copyBtn = new Button(activity);
        copyBtn.setText("📋 Copy");
        copyBtn.setTextColor(Color.WHITE);
        copyBtn.setTextSize(13);
        GradientDrawable cpybg = new GradientDrawable();
        cpybg.setColor(0xFF00BCD4);
        cpybg.setCornerRadius(12f);
        copyBtn.setBackground(cpybg);
        copyBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { copyLogsToClipboard(); }
        });
        row2.addView(copyBtn);
        mPanel.addView(row2);

        // Row 3: Clear
        LinearLayout row3 = new LinearLayout(activity);
        row3.setOrientation(LinearLayout.HORIZONTAL);

        Button clearBtn = new Button(activity);
        clearBtn.setText("🗑 Clear Logs");
        clearBtn.setTextColor(Color.WHITE);
        clearBtn.setTextSize(13);
        GradientDrawable clbg = new GradientDrawable();
        clbg.setColor(0xFFE53935);
        clbg.setCornerRadius(12f);
        clearBtn.setBackground(clbg);
        clearBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { clearLogs(); }
        });
        row3.addView(clearBtn);
        mPanel.addView(row3);

        ScrollView scroll = new ScrollView(activity);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 700);
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
        mPanel.addView(scroll);

        container.addView(mPanel);
        decor.addView(container);
        mRootView = container;
        mIsOpen = true;

        Log.i(TAG, "Mod menu shown");
    }

    private void updateStatus(final String s) {
        if (mCurrentActivity == null) return;
        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mStatusText != null) mStatusText.setText(s);
            }
        });
    }
}