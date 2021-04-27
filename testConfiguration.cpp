#include "Configuration.hpp"

int main()
{
    Configuration* conf = new Configuration("/home/user/zyp/dpmo/conf.xml");
    std::cout << conf->getClientCount() << std::endl;
    std::cout << conf->getServerCount() << std::endl;
    std::cout << conf->getServerIP() << std::endl;
    std::cout << conf->getServerNodeID() << std::endl;
    std::unordered_map<uint16_t, std::string> id2ip = conf->getID2IP();
    for (auto itr = id2ip.begin(); itr != id2ip.end(); itr++)
    {
        std::cout << "ID: " << itr->first << ", IP: " << itr->second
                  << std::endl;
    }
    return 0;
}
