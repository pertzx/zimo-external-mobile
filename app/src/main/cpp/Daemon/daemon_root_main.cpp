#include "DaemonApp.hpp"
#include "../Shared/Globals.hpp"

#include <android/log.h>
#include <signal.h>

#define LOGI(...) \
    __android_log_print( \
        ANDROID_LOG_INFO, \
        "StormDaemon", \
        __VA_ARGS__ \
    )

#define LOGE(...) \
    __android_log_print( \
        ANDROID_LOG_ERROR, \
        "StormDaemon", \
        __VA_ARGS__ \
    )

int main(
    int argc,
    char** argv
)
{
    (void)argc;
    (void)argv;

    signal(
        SIGPIPE,
        SIG_IGN
    );

    LOGI(
        "========================================"
    );

    LOGI(
        "stormdaemon root iniciado"
    );

    LOGI(
        "========================================"
    );

    DaemonApp::Run();

    LOGI(
        "stormdaemon root encerrado"
    );

    return 0;
}