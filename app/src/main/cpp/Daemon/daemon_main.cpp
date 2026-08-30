#include "DaemonApp.hpp"
#include <android/log.h>
#include <signal.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormDaemon", __VA_ARGS__)

int main(int argc, char** argv) {
    LOGI("Daemon Storm Cheats iniciado");

    // Ignorar SIGPIPE (quebra de socket)
    signal(SIGPIPE, SIG_IGN);

    DaemonApp::Run();
    return 0;
}