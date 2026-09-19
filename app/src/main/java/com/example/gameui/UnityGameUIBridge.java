package com.example.gameui;

import android.app.Activity;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;

public class UnityGameUIBridge {

    private static final String TAG = "LACMod-UI";

    // ─── Native methods ────────────────────────────────
    public static native void nativeInit(UnityGameUIBridge self);
    public static native void nativeRequestStartGame();
    public static native void nativeRequestExit();

    // ─── Singleton ─────────────────────────────────────
    private static UnityGameUIBridge sInstance;

    public static synchronized UnityGameUIBridge getInstance() {
        if (sInstance == null) {
            sInstance = new UnityGameUIBridge();
        }
        return sInstance;
    }

    // ─── State ─────────────────────────────────────────
    private View mRootView = null;
    private Activity mCurrentActivity = null;
    private TextView mStatusText = null;

    private UnityGameUIBridge() {
        Log.i(TAG, "Bridge created");
    }

    // ═══════════════════════════════════════════════════
    // Public API (called from C++)
    // ═══════════════════════════════════════════════════

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
        if (mCurrentActivity == null) return;

        mCurrentActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                removeView();
            }
        });
    }

    public void setGameState(int menu, boolean networkActive) {
        updateStatus("menu=" + menu + " net=" + networkActive);
    }

    public void setPlayerName(String name) {
        // unused
    }

    public void showJoinNotification() {
        updateStatus("Connecting...");
    }

    public void onCharacterEvent() {
        // unused
    }

    public void onBackMenuEvent() {
        // unused
    }

    public void onExitEvent() {
        updateStatus("Disconnected");
    }

    public void showWelcomeOnce(Activity activity) {
        // unused
    }

    // ═══════════════════════════════════════════════════
    // UI Building
    // ═══════════════════════════════════════════════════

    private void addView(Activity activity) {
        if (mRootView != null) return;

        ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();

        // ─── Full-screen transparent container ───
        FrameLayout container = new FrameLayout(activity);
        container.setLayoutParams(new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        container.setClickable(false);

        // ─── Menu panel ───
        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(40, 40, 40, 40);

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xDD1E1E1E);
        bg.setCornerRadius(24f);
        bg.setStroke(3, 0xFF00E5FF);
        panel.setBackground(bg);

        FrameLayout.LayoutParams panelLp = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT);
        panelLp.gravity = Gravity.TOP | Gravity.START;
        panelLp.leftMargin = 60;
        panelLp.topMargin = 200;
        panel.setLayoutParams(panelLp);
        panel.setElevation(20f);

        // ─── Title ───
        TextView title = new TextView(activity);
        title.setText("Mod Menu");
        title.setTextColor(0xFF00E5FF);
        title.setTextSize(18);
        title.setPadding(0, 0, 0, 20);
        panel.addView(title);

        // ─── Status ───
        mStatusText = new TextView(activity);
        mStatusText.setText("idle");
        mStatusText.setTextColor(0xFFAAAAAA);
        mStatusText.setTextSize(11);
        mStatusText.setPadding(0, 0, 0, 20);
        panel.addView(mStatusText);

        // ─── Join button ───
        Button joinBtn = new Button(activity);
        joinBtn.setText("Join Server");
        joinBtn.setTextColor(Color.WHITE);
        joinBtn.setTextSize(14);

        GradientDrawable jbg = new GradientDrawable();
        jbg.setColor(0xFF00C853);
        jbg.setCornerRadius(12f);
        joinBtn.setBackground(jbg);
        joinBtn.setPadding(30, 20, 30, 20);

        joinBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.i(TAG, "Join clicked");
                updateStatus("Joining...");
                nativeRequestStartGame();
            }
        });
        panel.addView(joinBtn);

        // ─── Close button ───
        Button closeBtn = new Button(activity);
        closeBtn.setText("Close");
        closeBtn.setTextColor(Color.WHITE);
        closeBtn.setTextSize(14);

        GradientDrawable cbg = new GradientDrawable();
        cbg.setColor(0xFFE53935);
        cbg.setCornerRadius(12f);
        closeBtn.setBackground(cbg);
        closeBtn.setPadding(30, 20, 30, 20);

        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT);
        closeLp.topMargin = 20;
        closeBtn.setLayoutParams(closeLp);

        closeBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                removeView();
            }
        });
        panel.addView(closeBtn);

        // ─── Attach ───
        container.addView(panel);
        decor.addView(container);
        mRootView = container;

        Log.i(TAG, "Mod menu shown");
    }

    private void removeView() {
        if (mRootView == null) return;

        ViewGroup parent = (ViewGroup) mRootView.getParent();
        if (parent != null) {
            parent.removeView(mRootView);
        }

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
