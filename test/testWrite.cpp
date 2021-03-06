#include "librdmapmem.hpp"

#define MAX_BUF_LEN 10
#define LAYOUT_NAME "intro_1"

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
    //创建了一个本地池
    uint64_t pop = rdmapmemobj_create(2, argv[argc - 1], LAYOUT_NAME,
                                      PMEMOBJ_MIN_POOL, 0666);

    if (pop == 0)
    {
        perror("pmemobj_create");
        return 1;
    }
    PMEMoid root = rdmapmemobj_root(pop, sizeof(struct Root));
    std::cout << root.off << std::endl;
    Root rootp;
    std::cin >> rootp.str;
    rootp.len = strlen(rootp.str);
    rdmapmem_direct_write(root, sizeof(Root), &rootp);
    rdmapmemobj_close(2, pop);
    DisConnectServer();
    return 0;
}
