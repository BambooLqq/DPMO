#include "librdmapmem.hpp"

#define MAX_BUF_LEN 10

struct Root
{
    size_t len;
    char str[MAX_BUF_LEN];
};

int main(int argc, char** argv)
{
    if (ConnectServer(argc - 1, argv))
    {
        std::cout << "testGetPoolData connected server successfully"
                  << std::endl;
    }
    else
    {
        std::cout << "testGetPoolData connected server failed" << std::endl;
        return 0;
    }

    PMEMoid root_obj;
    // root_obj.pool_uuid_lo =
    // root_obj.off =
    Root root;
    rdmapmem_direct_read(root_obj, sizeof(Root), &root);
    std::cout << root.len << std::endl;
    std::cout << root.str << std::endl;
    DisConnectServer();
    return 0;
}