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
    uint64_t pop = rdmapmemobj_open(2, argv[argc - 1], LAYOUT_NAME);

    if (pop == 0)
    {
        perror("pmemobj_open");
        return 1;
    }

    PMEMoid root = rdmapmemobj_root(pop, sizeof(struct Root));
    struct Root rootp;
    rdmapmem_direct_read(root, sizeof(Root), &rootp);
    std::cout << rootp.len << std::endl;
    std::cout << rootp.str << std::endl;
    rdmapmemobj_close(2, pop);
    DisConnectServer();
    return 0;
}