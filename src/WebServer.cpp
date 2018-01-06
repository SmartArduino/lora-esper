#include "WebServer.h"

WebServer::WebServer(IPAddress addr, int port) {
  this->__endpoints = nullptr;
  this->setIndexPath("/");
}

WebServer::WebServer(int port) {
  this->__endpoints = nullptr;
  this->setIndexPath("/");
}

WebServer::~WebServer(void) {
  for (size_t i = 0; i < this->__endpoints_count; i++) free(this->__endpoints[i]);
  free(this->__endpoints);
}

void WebServer::__index_fn(void) {
  String content = this->getFlashbag();
  content.concat(this->__index_content_prefix);

  content.concat("<p class='endpoints'>");
  for (size_t i = 0; i < this->__endpoints_count; i++) {
    if (this->__endpoints[i]->method != HTTP_GET && this->__endpoints[i]->method != HTTP_ANY) continue;

    content.concat("<a href='");
    content.concat(this->__endpoints[i]->path);
    content.concat("'><code>");
    content.concat(this->__endpoints[i]->path);
    content.concat("</code></a> - ");
    content.concat(this->__endpoints[i]->description);
    content.concat("<br />");
  }
  content.concat("</p>");

  this->send(200, "text/html", content.c_str());
}

void WebServer::setIndexPath(const char *path) {
  if (path && path[0] == '/' && (this->__index_path == NULL || strcmp(this->__index_path, path) == 0)) {
    if (this->__index_path) this->on(this->__index_path, HTTP_ANY, NULL);
    this->__index_path = path;
    this->on(this->__index_path, HTTP_ANY, [=](void){ this->__index_fn(); });
  }
}

const char* WebServer::getIndexPath(void) {
  return this->__index_path;
}

void WebServer::setIndexContentPrefix(const char *prefix) {
  this->__index_content_prefix = String(prefix);
}

String& WebServer::getIndexContentPrefix(void) {
  return this->__index_content_prefix;
}

void WebServer::addFlash(const char *message) {
  this->addFlash(NULL, message);
}

void WebServer::addFlash(const char *level, const char *message) {
  if (message &&  message[0] != '\0') {
    if (level && level[0] != '\0') {
      this->__flashbag.concat("<li class='flash flash-");
      this->__flashbag.concat(level);
      this->__flashbag.concat("'>");
    } else {
      this->__flashbag.concat("<li class='flash'>");
    }
    this->__flashbag.concat(message);
    this->__flashbag.concat("</li>");
  }
}

String WebServer::getFlashbag() {
  if (this->__flashbag.length() == 0) return "";

  String flashbag =
    "<style>"
      "ul.flashbag {"
        "padding: 0;"
        "list-style: none;"
      "}"
      "ul.flashbag li {"
        "padding: 10px;"
        "background: #efeeee;"
        "border: 1px solid white;"
      "}"
    "</style>"
    "<ul class='flashbag'>";

  flashbag.concat(this->__flashbag);
  flashbag.concat("</li>");

  this->__flashbag = "";

  return flashbag;
}

int WebServer::addEndpoint(const char *path, const char *description, THandlerFunction fn) {
  return this->addEndpoint(path, description, HTTP_ANY, fn);
}

int WebServer::addEndpoint(const char *path, const char *description, HTTPMethod method, THandlerFunction fn) {
  if (path == NULL || path[0] != '/') return 1;

  struct webserver_endpoint* endpoint = (webserver_endpoint*) malloc(sizeof(webserver_endpoint));
  endpoint->path = path;
  endpoint->description = description;
  endpoint->method = method;
  endpoint->callback = fn;

  this->__endpoints_count++;
  this->__endpoints = (webserver_endpoint**) realloc(this->__endpoints, this->__endpoints_count * sizeof(struct webserver_endpoint*));
  this->__endpoints[this->__endpoints_count - 1] = endpoint;

  this->on(path, method, fn);

  return 0;
}

void WebServer::send(int code, const char *content_type, const String &content) {
  boolean send_flashbag = (
    strcmp(content_type, "text/html") == 0 &&
    (code >= 200 && code < 300 && code >= 400 && code < 600 &&
      code != 204 && code != 205 && code != 207 && code != 208)
  );

  this->send(code, content_type, content, send_flashbag);
}

void WebServer::send(int code, const char *content_type, const String &content, boolean send_flashbag) {
  if (send_flashbag) {
    String content_with_flashbag = this->getFlashbag();
    if (content_with_flashbag.length() > 0) {
      content_with_flashbag.concat(content);
    } else {
      content_with_flashbag = content;
    }

    ESP8266WebServer::send(code, content_type, content_with_flashbag);
  } else {
    ESP8266WebServer::send(code, content_type, content);
  }
}
