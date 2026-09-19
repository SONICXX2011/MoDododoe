#pragma once

#include <jni.h>
#include <string>

extern "C" {
    JNIEXPORT void JNICALL
    Java_com_example_gameui_UnityGameUIBridge_nativeInit(
        JNIEnv*, jclass, jobject);

    JNIEXPORT void JNICALL
    Java_com_example_gameui_UnityGameUIBridge_nativeRequestStartGame(
        JNIEnv*, jclass);

    JNIEXPORT void JNICALL
    Java_com_example_gameui_UnityGameUIBridge_nativeRequestExit(
        JNIEnv*, jclass);
}

// ─── Called from C++ side ───
void JB_Show();
void JB_Hide();
void JB_SetGameState(int menu, bool networkActive);
void JB_ShowJoinNotification();
void JB_OnExitEvent();
