#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/typeof/typeof.hpp>
#include <iostream>
#include <string>
#include <unordered_map>

class Configuration
{
private:
    boost::property_tree::ptree pt_;
    std::unordered_map<uint16_t, std::string> id2ip_; // for client
    std::unordered_map<std::string, uint16_t> ip2id_; // for client

    std::string server_ip_;
    uint16_t server_nodeid_; // default = 0
    int server_count_;       // default = 1 server's nodeid = 0;
    int client_count_;       //

public:
    Configuration();
    Configuration(std::string config_file_path);
    ~Configuration();
    std::string getIPbyID(uint16_t id);
    uint16_t getIDbyIP(std::string ip);
    std::unordered_map<uint16_t, std::string> getID2IP();
    std::unordered_map<std::string, uint16_t> getIP2ID();
    int getClientCount();
    int getServerCount();
    int getServerNodeID();
    bool addClient(uint16_t nodeid, std::string client_ip);
    bool deleteClientByID(uint16_t nodeid);
    bool deleteClientByIP(std::string client_ip);
    std::string getServerIP();
};
#endif // !_CONFIGURATION_H_
