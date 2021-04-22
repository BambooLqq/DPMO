#include "Configuration.h"

#include "Debug.h"

Configuration::Configuration() {
  server_count_ = 0;
  client_count_ = 0;
  server_nodeid_ = 0;
  std::string path = "../conf.xml";
  char resolved_path[1024];
  memset(resolved_path, 0, 1024);
  realpath(path.c_str(), resolved_path);
  std::cout << resolved_path << std::endl;
  boost::property_tree::xml_parser::read_xml(resolved_path, pt_);

  boost::property_tree::ptree child = pt_.get_child("address");
  boost::property_tree::ptree client = child.get_child("client");
  boost::property_tree::ptree server = child.get_child("server");

  if (server.empty()) {
    Debug::notifyError("No Server!");
    exit(1);
  } else if (server.size() > 1) {
    Debug::notifyError("We just support only one server!");
  } else {
    server_count_ = 1;
    server_nodeid_ = 0;
    server_ip_ = server.get<std::string>("ip");
  }

  for (BOOST_AUTO(pos, client.begin()); pos != client.end(); pos++) {
    uint16_t nodeid = (uint16_t)pos->second.get<int>("id");
    std::string clientip = pos->second.get<std::string>("ip");
    if (nodeid == 0) {
      Debug::notifyError("Client's node id must not equals 0!");
      exit(1);
    }
    if (ip2id_.find(clientip) != ip2id_.end()) {
      Debug::notifyError("ip %s is existed!", clientip.c_str());
      exit(1);
    }
    if (id2ip_.find(nodeid) != id2ip_.end()) {
      Debug::notifyError("id %d is existed!", nodeid);
      exit(1);
    }
    id2ip_.insert(std::pair<uint16_t, std::string>(nodeid, clientip));
    ip2id_.insert(std::pair<std::string, uint16_t>(clientip, nodeid));
    client_count_++;
  }

  if (client_count_ == 0) {
    Debug::notifyError("No Client");
    exit(1);
  }
}

Configuration::Configuration(std::string config_file_path) {
  server_count_ = 0;
  client_count_ = 0;
  server_nodeid_ = 0;
  boost::property_tree::xml_parser::read_xml(config_file_path, pt_);

  boost::property_tree::ptree child = pt_.get_child("address");
  boost::property_tree::ptree client = child.get_child("client");
  boost::property_tree::ptree server = child.get_child("server");

  if (server.empty()) {
    Debug::notifyError("No Server!");
    exit(1);
  } else if (server.size() > 1) {
    Debug::notifyError("We just support only one server!");
  } else {
    server_count_ = 1;
    server_nodeid_ = 0;
    server_ip_ = server.get<std::string>("ip");
  }

  for (BOOST_AUTO(pos, client.begin()); pos != client.end(); pos++) {
    uint16_t nodeid = (uint16_t)pos->second.get<int>("id");
    std::string clientip = pos->second.get<std::string>("ip");
    if (nodeid == 0) {
      Debug::notifyError("Client's node id must not equals 0!");
      exit(1);
    }
    if (ip2id_.find(clientip) != ip2id_.end()) {
      Debug::notifyError("ip %s is existed!", clientip.c_str());
      exit(1);
    }
    if (id2ip_.find(nodeid) != id2ip_.end()) {
      Debug::notifyError("id %d is existed!", nodeid);
      exit(1);
    }
    id2ip_.insert(std::pair<uint16_t, std::string>(nodeid, clientip));
    ip2id_.insert(std::pair<std::string, uint16_t>(clientip, nodeid));
    client_count_++;
  }

  if (client_count_ == 0) {
    Debug::notifyError("No Client");
    exit(1);
  }
}

Configuration::~Configuration() {
  Debug::notifyInfo("Configuration is closed successfully.");
}

std::string Configuration::getIPbyID(uint16_t id) {
  if (id2ip_.find(id) != id2ip_.end()) {
    return id2ip_[id];
  }
  return "Not Found";
}

uint16_t Configuration::getIDbyIP(std::string ip) {
  if (ip2id_.find(ip) != ip2id_.end()) {
    return ip2id_[ip];
  }
  return -1;
}

std::unordered_map<uint16_t, std::string> Configuration::getID2IP() {
  return id2ip_;
}

std::unordered_map<std::string, uint16_t> Configuration::getIP2ID() {
  return ip2id_;
}
int Configuration::getClientCount() { return client_count_; }

int Configuration::getServerCount() { return server_count_; }

int Configuration::getServerNodeID() { return this->server_nodeid_; }

std::string Configuration::getServerIP() { return server_ip_; }

bool Configuration::addClient(uint16_t nodeid, std::string client_ip) {
  if (id2ip_.find(nodeid) != id2ip_.end() &&
      ip2id_.find(client_ip) != ip2id_.end()) {
    client_count_++;
    id2ip_.insert(std::pair<uint16_t, std::string>(nodeid, client_ip));
    ip2id_.insert(std::pair<std::string, uint16_t>(client_ip, nodeid));
    return true;
  }
  Debug::debugItem("Configuration::AddClient:");
  Debug::notifyError("Existing the NodeID %d or ClientIp %s", nodeid,
                     client_ip.c_str());
  return false;
}

bool Configuration::deleteClientByID(uint16_t nodeid) {
  if (id2ip_.find(nodeid) == id2ip_.end()) {
    Debug::debugItem("Configuration::deleteClientByID:");
    Debug::notifyError("Do not exist the nodeid %d", nodeid);
    return false;
  }
  client_count_--;
  id2ip_.erase(nodeid);
  ip2id_.erase(id2ip_.find(nodeid)->second);
  return true;
}

bool Configuration::deleteClientByIP(std::string client_ip) {
  if (ip2id_.find(client_ip) == ip2id_.end()) {
    Debug::debugItem("Configuration::deleteClientByIP:");
    Debug::notifyError("Do not exist the clientIp %s", client_ip.c_str());
    return false;
  }
  client_count_--;
  ip2id_.erase(client_ip);
  id2ip_.erase(ip2id_.find(client_ip)->second);
  return true;
}
