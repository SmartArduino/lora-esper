#ifndef __LORA_ESPER_WEBSERVER_H__
#define __LORA_ESPER_WEBSERVER_H__

class WebServer;

#include <ESP8266WebServer.h>

typedef struct webserver_endpoint {
  String path;
  String description;
  HTTPMethod method;
  ESP8266WebServer::THandlerFunction callback;
} webserver_endpoint;

class WebServer : public ESP8266WebServer {
private:
  struct webserver_endpoint** __endpoints;
  size_t __endpoints_count = 0;
  String __index_path;

  void __index_fn(void);

public:
  WebServer(IPAddress addr, int port = 80);
  WebServer(int port = 80);
  ~WebServer(void);
  void setIndexPath(String path);
  String getIndexPath(void);
  void addEndpoint(String path, String description, THandlerFunction fn);
  void addEndpoint(String path, String description, HTTPMethod method, THandlerFunction fn);
};

#endif // __LORA_ESPER_WEBSERVER_H__
