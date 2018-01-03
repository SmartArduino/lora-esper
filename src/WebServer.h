#ifndef __LORA_ESPER_WEBSERVER_H__
#define __LORA_ESPER_WEBSERVER_H__

class WebServer;

#include <ESP8266WebServer.h>

typedef struct webserver_endpoint {
  const char *path;
  const char *description;
  HTTPMethod method;
  ESP8266WebServer::THandlerFunction callback;
} webserver_endpoint;

class WebServer : public ESP8266WebServer {
private:
  struct webserver_endpoint** __endpoints;
  size_t __endpoints_count = 0;
  const char *__index_path;
  String __index_content_prefix;
  String __flashbag;

  void __index_fn(void);

public:
  WebServer(IPAddress addr, int port = 80);
  WebServer(int port = 80);
  ~WebServer(void);
  void setIndexPath(const char *path);
  const char* getIndexPath(void);
  void setIndexContentPrefix(const char *prefix);
  String& getIndexContentPrefix(void);
  void addFlash(const char *message);
  void addFlash(const char *level, const char *message);
  String getFlashbag(void);
  int addEndpoint(const char *path, const char *description, THandlerFunction fn);
  int addEndpoint(const char *path, const char *description, HTTPMethod method, THandlerFunction fn);
  void send(int code, const char *content_type, const String &content);
  void send(int code, const char *content_type, const String &content, boolean send_flashbag);
};

#endif // __LORA_ESPER_WEBSERVER_H__
