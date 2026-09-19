#pragma once

#include <jni.h>
#include <atomic>
#include <mutex>
#include <string>

// ─── JVM / Java bridge ───
extern JavaVM*   g_vm;
extern jclass    g_bridgeClass;
extern jobject   g_bridgeInstance;

// ─── Cached method IDs ───
extern jmethodID mid_show;
extern jmethodID mid_hide;
extern jmethodID mid_setGameState;
extern jmethodID mid_setPlayerName;
extern jmethodID mid_showJoinNotification;
extern jmethodID mid_onCharacterEvent;
extern jmethodID mid_onBackMenuEvent;
extern jmethodID mid_onExitEvent;

// ─── Server config ───
constexpr const char* SERVER_IP   = "5.26.26";
constexpr int         SERVER_PORT = 2626;

// ─── Runtime state ───
struct RuntimeState {
    std::atomic<bool> networkSuppressed{false};
    std::atomic<bool> lastNetworkActive{false};
    std::atomic<int>  lastMenu{-1};
};

extern RuntimeState g_state;

// ─── JNI helpers ───
JNIEnv* GetEnv();
bool    AttachThread(JNIEnv** env, bool* needsDetach);
void    DetachThreadIfNeeded(bool needsDetach);
