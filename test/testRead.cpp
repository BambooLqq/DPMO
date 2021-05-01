#include "librdmapmem.hpp"

#define LAYOUT_NAME "intro_1"
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
        std::cout << "test1 connected server successfully" << std::endl;
    }
    else
    {
        std::cout << "test1 connected server failed" << std::endl;
        return 0;
    }
    PMEMobjpool* pop = rdmapmemobj_open(argv[argc - 1], LAYOUT_NAME);

    if (pop == NULL)
    {
        perror("pmemobj_open");
        return 1;
    }

    PMEMoid root = pmemobj_root(pop, sizeof(struct Root));
    struct Root rootp;
    rdmapmem_direct_read(root, sizeof(Root), &rootp);
    std::cout << rootp.len << std::endl;
    std::cout << rootp.str << std::endl;
    rdmapmemobj_close(pop);
    DisConnectServer();
    return 0;
}