#ifndef CAIYU_WEB_SERVER_H
#define CAIYU_WEB_SERVER_H

#include <cJSON.h>
#include <esp_http_server.h>

#include <cstddef>
#include <string>

#include "device_state.h"

class CaiyuDisplay;
class StripController;

namespace caiyu {
class Actionments;
}

class CaiyuWebServer {
public:
    CaiyuWebServer(CaiyuDisplay* display, StripController* strip, caiyu::Actionments* actionments,
                   int port = 80);
    ~CaiyuWebServer();

    bool Start();
    void Stop();

    bool started() const { return server_ != nullptr; }
    int port() const { return port_; }

    bool HasClients() const;

    void SyncChatState();

private:
    static constexpr int kMaxSockets = 5;
    static constexpr size_t kMaxMessageLen = 1024;

    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t StyleHandler(httpd_req_t* req);
    static esp_err_t ScriptHandler(httpd_req_t* req);
    static esp_err_t FaviconHandler(httpd_req_t* req);
    static esp_err_t WsHandler(httpd_req_t* req);

    static esp_err_t SendEmbedded(httpd_req_t* req, const char* content_type, const uint8_t* begin,
                                  const uint8_t* end);

    esp_err_t OnWebSocket(httpd_req_t* req);
    void HandleMessage(httpd_req_t* req, const char* data, size_t len);
    void HandleEmotion(httpd_req_t* req, const cJSON* root);
    void HandleLamp(httpd_req_t* req, const cJSON* root);
    void SendLampState(httpd_req_t* req);
    void HandleAction(httpd_req_t* req, const cJSON* root);
    void SendActionState(httpd_req_t* req);
    void HandleDrive(httpd_req_t* req, const cJSON* root);
    void SendDriveState(httpd_req_t* req);
    void HandleChat(httpd_req_t* req, const cJSON* root);
    void SendChatState(httpd_req_t* req);
    void BroadcastChatState();
    void HandleTrim(httpd_req_t* req, const cJSON* root);
    void SendTrimState(httpd_req_t* req);
    void HandleSpeed(httpd_req_t* req, const cJSON* root);
    void SendSpeedState(httpd_req_t* req);
    void BroadcastJson(const std::string& message);

    static void SendText(httpd_req_t* req, const std::string& message);
    void AddClient(int fd);
    void RemoveClient(int fd);

    CaiyuDisplay* display_ = nullptr;
    StripController* strip_ = nullptr;
    caiyu::Actionments* actionments_ = nullptr;
    int port_ = 80;
    httpd_handle_t server_ = nullptr;
    int clients_[kMaxSockets] = {};
    DeviceState last_chat_state_ = kDeviceStateUnknown;

    static CaiyuWebServer* instance_;
};

#endif
