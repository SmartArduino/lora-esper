#include "WebServer.h"

WebServer::WebServer(IPAddress addr, int port) {
  this->__endpoints = nullptr;
}

WebServer::WebServer(int port) {
  this->__endpoints = nullptr;
}

WebServer::~WebServer(void) {
  for (size_t i; i < this->__endpoints_count; i++) free(this->__endpoints[i]);
  free(this->__endpoints);
}

void WebServer::__index_fn(void) {
  // TODO: implement dat shit yo!
}

void WebServer::setIndexPath(String path) {
  if (this->__index_path == path) {
    this->on(this->__index_path.c_str(), HTTP_ANY, NULL);
    this->__index_path = path;
    this->on(this->__index_path.c_str(), HTTP_ANY, [=](void){ this->__index_fn(); });
  }
}

String WebServer::getIndexPath(void) {
  return this->__index_path;
}

void WebServer::addEndpoint(String path, String description, THandlerFunction fn) {
  this->addEndpoint(path, description, HTTP_ANY, fn);
}

void WebServer::addEndpoint(String path, String description, HTTPMethod method, THandlerFunction fn) {

  struct webserver_endpoint* endpoint = (webserver_endpoint*) malloc(sizeof(webserver_endpoint));
  endpoint->path = path;
  endpoint->description = description;
  endpoint->method = method;
  endpoint->callback = fn;

  this->__endpoints_count++;
  this->__endpoints = (webserver_endpoint**) realloc(this->__endpoints, this->__endpoints_count * sizeof(struct webserver_endpoint*));
  this->__endpoints[this->__endpoints_count - 1] = endpoint;

  this->on(path.c_str(), method, fn);
}
