#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/typeof/typeof.hpp>
#include <iostream>
#include <string>
#include <unordered_map>

class Configuration {
 private:
  boost::property_tree::ptree pt;
  std::unordered_map<uint16_t, std::string> id2ip;  // for client
  std::unordered_map<std::string, uint16_t> ip2id;  // for client

  std::string server_ip;
  uint16_t server_nodeid;  // default = 0
  int server_count;        // default = 1 server's nodeid = 0;
  int client_count;        //

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
  std::string getServerIP();
};
#endif  // !_CONFIGURATION_H_
