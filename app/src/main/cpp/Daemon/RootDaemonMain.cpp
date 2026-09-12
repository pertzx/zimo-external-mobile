#include "DaemonApp.hpp"

#include <unistd.h>
#include <signal.h>
#include <cstdio>

int main()
{
    const char* pidFile =
        "/data/local/tmp/storm_daemon.pid";

    {
        FILE* file = std::fopen(pidFile, "w");

        if (file)
        {
            std::fprintf(
                file,
                "%d\n",
                static_cast<int>(getpid())
            );

            std::fclose(file);
        }
    }

    /*
     * O processo será encerrado pelo Java via kill().
     * Isso fecha o socket automaticamente.
     */
    DaemonApp::Run();

    unlink(pidFile);

    return 0;
}