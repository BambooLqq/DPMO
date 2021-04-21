#include "Configuration.h"

#include "Debug.h"

Configuration::Configuration() {
  server_count = 0;
  client_count = 0;
  server_nodeid = 0;
  boost::property_tree::xml_parser::read_xml("../conf.xml", pt);

  boost::property_tree::ptree child = pt.get_child("address");
  boost::property_tree::ptree client = child.get_child("client");
  boost::property_tree::ptree server = child.get_child("server");

  for (BOOST_AUTO(pos, child.begin()); pos != child.end(); pos++) {
    if (pos->first == "<client>") {
      uint16_t nodeid = (uint16_t)pos->second.get<int>("id");
      std::string clientip = pos->second.get<std::string>("ip");
      if (nodeid == 0) {
        Debug::notifyError("Client's node id must not equals 0!");
        exit(1);
      }
      if (ip2id.find(clientip) != ip2id.end()) {
        Debug::notifyError("ip %s is existed!");
        exit(1);
      }
      if (id2ip.find(nodeid) != id2ip.end()) {
        Debug::notifyError("id %s is existed!");
        exit(1);
      }
      id2ip.insert(std::pair<uint16_t, std::string>(nodeid, clientip));
      ip2id.insert(std::pair<std::string, uint16_t>(clientip, nodeid));
      client_count++;
    } else if (pos->first == "<server>") {
      if (server_count >= 1) {
        Debug::notifyError("We just support only one server!");
        exit(1);
      } else {
        server_count++;
        server_ip = pos->second.get<std::string>("ip");
      }
    }
  }
  if (client_count == 0 || server_count == 0) {
    Debug::notifyError("There is no clients or servers");
    exit(1);
  }
}

Configuration::Configuration(std::string config_file_path) {
  server_count = 0;
  client_count = 0;
  server_nodeid = 0;
  boost::property_tree::xml_parser::read_xml(config_file_path, pt);

  boost::property_tree::ptree child = pt.get_child("address");
  boost::property_tree::ptree client = child.get_child("client");
  boost::property_tree::ptree server = child.get_child("server");

  if (server.empty()) {
    Debug::notifyError("No Server!");
    exit(1);
  } else if (server.size() > 1) {
    Debug::notifyError("We just support only one server!");
  } else {
    server_count = 1;
    server_nodeid = 0;
    server_ip = server.get<std::string>("ip");
  }

  for (BOOST_AUTO(pos, client.begin()); pos != client.end(); pos++) {
    uint16_t nodeid = (uint16_t)pos->second.get<int>("id");
    std::string clientip = pos->second.get<std::string>("ip");
    if (nodeid == 0) {
      Debug::notifyError("Client's node id must not equals 0!");
      exit(1);
    }
    if (ip2id.find(clientip) != ip2id.end()) {
      Debug::notifyError("ip %s is existed!", clientip.c_str());
      exit(1);
    }
    if (id2ip.find(nodeid) != id2ip.end()) {
      Debug::notifyError("id %d is existed!", nodeid);
      exit(1);
    }
    id2ip.insert(std::pair<uint16_t, std::string>(nodeid, clientip));
    ip2id.insert(std::pair<std::string, uint16_t>(clientip, nodeid));
    client_count++;
  }

  if (client_count == 0) {
    Debug::notifyError("No Client");
    exit(1);
  }
}

Configuration::~Configuration() {
  Debug::notifyInfo("Configuration is closed successfully.");
}

std::string Configuration::getIPbyID(uint16_t id) {
  if (id2ip.find(id) != id2ip.end()) {
    return id2ip[id];
  }
  return "Not Found";
}

uint16_t Configuration::getIDbyIP(std::string ip) {
  if (ip2id.find(ip) != ip2id.end()) {
    return ip2id[ip];
  }
  return -1;
}

std::unordered_map<uint16_t, std::string> Configuration::getID2IP() {
  return id2ip;
}

std::unordered_map<std::string, uint16_t> Configuration::getIP2ID() {
  return ip2id;
}
int Configuration::getClientCount() { return client_count; }

int Configuration::getServerCount() { return server_count; }

int Configuration::getServerNodeID() { return this->server_nodeid; }

std::string Configuration::getServerIP() { return server_ip; }