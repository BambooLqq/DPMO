#include "RdmaSocket.hpp"

int main()
{
    char* buf = (char*)malloc(128);
    Configuration* conf = new Configuration();
    if (buf)
    {
        std::cout << (void*)buf << std::endl;
        memset(buf, 0, 128);
    }

    RdmaSocket* sock = new RdmaSocket((uint64_t)buf, 128, conf, false, 0);
    delete sock;
    delete conf;
    free(buf);
}
