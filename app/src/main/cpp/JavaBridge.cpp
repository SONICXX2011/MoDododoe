#include "JavaBridge.h"
#include "Globals.h"
#include "ModCore.h"

#include <android/log.h>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

jmethodID mid_show                 = nullptr;
jmethodID mid_hide                 = nullptr;
jmethodID mid_setGameState         = nullptr;
jmethodID mid_setPlayerName        = nullptr;
jmethodID mid_showJoinNotification = nullptr;
jmethodID mid_onCharacterEvent     = nullptr;
jmethodID mid_onBackMenuEvent      = nullptr;
jmethodID mid_onExitEvent          = nullptr;
jmethodID mid_log                  = nullptr;

static jobject GetActivity(JNIEnv* env) {
    jclass up = env->FindClass("com/unity3d/player/UnityPlayer");
    if (!up) {
        env->ExceptionClear();
        return nullptr;
    }
    jfieldID fid = env->GetStaticFieldID(
        up,
        "currentActivity",
        "Landroid/app/Activity;");
    if (!fid) {
        env->ExceptionClear();
        return nullptr;
    }
    return env->GetStaticObjectField(up, fid);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_gameui_UnityGameUIBridge_nativeInit(
    JNIEnv* env,
    jclass,
    jobject bridgeInstance)
{
    if (!bridgeInstance) {
        LOGE("nativeInit: bridge is null");
        return;
    }

    g_bridgeInstance = env->NewGlobalRef(bridgeInstance);

    jclass cls = env->GetObjectClass(bridgeInstance);
    g_bridgeClass = (jclass) env->NewGlobalRef(cls);
    env->DeleteLocalRef(cls);

    mid_show = env->GetMethodID(
        g_bridgeClass,
        "show",
        "(Landroid/app/Activity;)V");

    mid_hide = env->GetMethodID(
        g_bridgeClass,
        "hide",
        "()V");

    mid_setGameState = env->GetMethodID(
        g_bridgeClass,
        "setGameState",
        "(IZ)V");

    mid_setPlayerName = env->GetMethodID(
        g_bridgeClass,
        "setPlayerName",
        "(Ljava/lang/String;)V");

    mid_showJoinNotification = env->GetMethodID(
        g_bridgeClass,
        "showJoinNotification",
        "()V");

    mid_onCharacterEvent = env->GetMethodID(
        g_bridgeClass,
        "onCharacterEvent",
        "()V");

    mid_onBackMenuEvent = env->GetMethodID(
        g_bridgeClass,
        "onBackMenuEvent",
        "()V");

    mid_onExitEvent = env->GetMethodID(
        g_bridgeClass,
        "onExitEvent",
        "()V");

    mid_log = env->GetMethodID(
        g_bridgeClass,
        "log",
        "(Ljava/lang/String;)V");

    LOGI("nativeInit: bridge cached (log=%p)", mid_log);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_gameui_UnityGameUIBridge_nativeRequestStartGame(
    JNIEnv*, jclass)
{
    LOGI("JNI: StartGame requested");
    TriggerStartGame();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_gameui_UnityGameUIBridge_nativeRequestExit(
    JNIEnv*, jclass)
{
    LOGI("JNI: Exit requested");
}

static void CallVoid(jmethodID mid) {
    if (!mid || !g_bridgeInstance) return;
    bool detach = false;
    JNIEnv* env = nullptr;
    if (!AttachThread(&env, &detach)) return;
    env->CallVoidMethod(g_bridgeInstance, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    DetachThreadIfNeeded(detach);
}

void JB_Show() {
    if (!mid_show || !g_bridgeInstance) return;
    bool detach = false;
    JNIEnv* env = nullptr;
    if (!AttachThread(&env, &detach)) return;
    jobject activity = GetActivity(env);
    if (activity) {
        env->CallVoidMethod(g_bridgeInstance, mid_show, activity);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(activity);
    }
    DetachThreadIfNeeded(detach);
}

void JB_Hide() {
    CallVoid(mid_hide);
}

void JB_ShowJoinNotification() {
    CallVoid(mid_showJoinNotification);
}

void JB_OnExitEvent() {
    CallVoid(mid_onExitEvent);
}

void JB_SetGameState(int menu, bool networkActive) {
    if (!mid_setGameState || !g_bridgeInstance) return;
    bool detach = false;
    JNIEnv* env = nullptr;
    if (!AttachThread(&env, &detach)) return;
    env->CallVoidMethod(
        g_bridgeInstance,
        mid_setGameState,
        (jint) menu,
        (jboolean) networkActive);
    if (env->ExceptionCheck()) env->ExceptionClear();
    DetachThreadIfNeeded(detach);
}

void JB_Log(const std::string& msg) {
    if (!mid_log || !g_bridgeInstance) return;
    bool detach = false;
    JNIEnv* env = nullptr;
    if (!AttachThread(&env, &detach)) return;
    jstring jmsg = env->NewStringUTF(msg.c_str());
    env->CallVoidMethod(g_bridgeInstance, mid_log, jmsg);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jmsg);
    DetachThreadIfNeeded(detach);
}