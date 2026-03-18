#include "TcpServer.h"

int main() {
    TcpServer server(8888);

    if (!server.Start()) {
        return 1;
    }

    server.Run();
    server.Stop();

    return 0;
}