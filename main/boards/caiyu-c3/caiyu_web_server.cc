#include "caiyu_web_server.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>

#include "application.h"
#include "caiyu_actionments.h"
#include "caiyu_display.h"
#include "caiyu_emotion.h"
#include "strip_controller.h"

#define TAG "CaiyuWeb"

extern const uint8_t caiyu_index_html_start[] asm("_binary_caiyu_index_html_start");
extern const uint8_t caiyu_index_html_end[] asm("_binary_caiyu_index_html_end");
extern const uint8_t caiyu_style_css_start[] asm("_binary_caiyu_style_css_start");
extern const uint8_t caiyu_style_css_end[] asm("_binary_caiyu_style_css_end");
extern const uint8_t caiyu_app_js_start[] asm("_binary_caiyu_app_js_start");
extern const uint8_t caiyu_app_js_end[] asm("_binary_caiyu_app_js_end");
extern const uint8_t caiyu_favicon_svg_start[] asm("_binary_caiyu_favicon_svg_start");
extern const uint8_t caiyu_favicon_svg_end[] asm("_binary_caiyu_favicon_svg_end");

CaiyuWebServer* CaiyuWebServer::instance_ = nullptr;

namespace {

std::string BuildHelloMessage() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "board", BOARD_TYPE);
    const esp_app_desc_t* app = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware", (app != nullptr) ? app->version : "unknown");

    cJSON* emotions = cJSON_AddArrayToObject(root, "emotions");
    size_t count = 0;
    const caiyu::EmotionNameEntry* table = caiyu::EmotionNameTable(&count);
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(emotions, cJSON_CreateString(table[i].name));
    }

    cJSON* actions = cJSON_AddArrayToObject(root, "actions");
    for (size_t i = 0; i < static_cast<size_t>(caiyu::Actionments::Action::kCount); ++i) {
        const auto index = static_cast<caiyu::Actionments::Action>(i);
        cJSON_AddItemToArray(actions, cJSON_CreateString(caiyu::Actionments::ActionName(index)));
    }

    char* text = cJSON_PrintUnformatted(root);
    std::string message = (text != nullptr) ? text : R"({"type":"hello"})";
    if (text != nullptr) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    return message;
}

const std::string& HelloMessage() {
    static const std::string message = BuildHelloMessage();
    return message;
}

uint8_t JsonByte(const cJSON* root, const char* key, uint8_t fallback) {
    const cJSON* item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    const int value = item->valueint;
    if (value < 0) {
        return 0;
    }
    return (value > 255) ? static_cast<uint8_t>(255) : static_cast<uint8_t>(value);
}

std::string BuildChatStateMessage(DeviceState state) {
    std::string message = R"({"type":"chat","state":")";
    message += DeviceStateMachine::GetStateName(state);
    message += "\"}";
    return message;
}

std::string BuildTrimMessage(const int trims[], int max_trim, bool dirty) {
    std::string message = R"({"type":"trim","trims":[)";
    for (int i = 0; i < caiyu::kLegCount; ++i) {
        if (i != 0) {
            message += ',';
        }
        message += std::to_string(trims[i]);
    }
    message += R"(],"max":)";
    message += std::to_string(max_trim);
    message += R"(,"dirty":)";
    message += dirty ? "true" : "false";
    message += '}';
    return message;
}

struct ChatBroadcastJob {
    httpd_handle_t server;
    int fd;
    char* payload;
    size_t len;
};

void ChatBroadcastJobFn(void* arg) {
    auto* job = static_cast<ChatBroadcastJob*>(arg);
    if (httpd_ws_get_fd_info(job->server, job->fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.final = true;
        frame.payload = reinterpret_cast<uint8_t*>(job->payload);
        frame.len = job->len;
        httpd_ws_send_frame_async(job->server, job->fd, &frame);
    }
    free(job->payload);
    free(job);
}

}

CaiyuWebServer::CaiyuWebServer(CaiyuDisplay* display, StripController* strip,
                               caiyu::Actionments* actionments, int port)
    : display_(display), strip_(strip), actionments_(actionments), port_(port) {
    instance_ = this;
}

CaiyuWebServer::~CaiyuWebServer() {
    Stop();
    instance_ = nullptr;
}

bool CaiyuWebServer::Start() {
    if (server_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = port_ + 1;
    config.max_open_sockets = kMaxSockets;
    config.stack_size = 4096;
    config.task_priority = tskIDLE_PRIORITY + 3;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed on port %d", port_);
        server_ = nullptr;
        return false;
    }

    static const httpd_uri_t kRoutes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = RootHandler, .user_ctx = nullptr},
        {.uri = "/index.html", .method = HTTP_GET, .handler = RootHandler, .user_ctx = nullptr},
        {.uri = "/style.css", .method = HTTP_GET, .handler = StyleHandler, .user_ctx = nullptr},
        {.uri = "/app.js", .method = HTTP_GET, .handler = ScriptHandler, .user_ctx = nullptr},
        {.uri = "/favicon.svg", .method = HTTP_GET, .handler = FaviconHandler, .user_ctx = nullptr},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = FaviconHandler, .user_ctx = nullptr},
        {.uri = "/ws",
         .method = HTTP_GET,
         .handler = WsHandler,
         .user_ctx = nullptr,
         .is_websocket = true},
    };
    for (const auto& route : kRoutes) {
        httpd_register_uri_handler(server_, &route);
    }

    ESP_LOGI(TAG, "control page ready on port %d", port_);
    return true;
}

void CaiyuWebServer::Stop() {
    if (server_ == nullptr) {
        return;
    }
    httpd_stop(server_);
    server_ = nullptr;
    memset(clients_, 0, sizeof(clients_));
    ESP_LOGI(TAG, "control page stopped");
}

esp_err_t CaiyuWebServer::SendEmbedded(httpd_req_t* req, const char* content_type,
                                       const uint8_t* begin, const uint8_t* end) {
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, reinterpret_cast<const char*>(begin),
                           static_cast<ssize_t>(end - begin));
}

esp_err_t CaiyuWebServer::RootHandler(httpd_req_t* req) {
    return SendEmbedded(req, "text/html; charset=utf-8", caiyu_index_html_start,
                        caiyu_index_html_end);
}

esp_err_t CaiyuWebServer::StyleHandler(httpd_req_t* req) {
    return SendEmbedded(req, "text/css; charset=utf-8", caiyu_style_css_start, caiyu_style_css_end);
}

esp_err_t CaiyuWebServer::ScriptHandler(httpd_req_t* req) {
    return SendEmbedded(req, "application/javascript; charset=utf-8", caiyu_app_js_start,
                        caiyu_app_js_end);
}

esp_err_t CaiyuWebServer::FaviconHandler(httpd_req_t* req) {
    return SendEmbedded(req, "image/svg+xml", caiyu_favicon_svg_start, caiyu_favicon_svg_end);
}

void CaiyuWebServer::AddClient(int fd) {
    for (int& slot : clients_) {
        if (slot == fd) {
            return;
        }
    }
    for (int& slot : clients_) {
        if (slot == 0) {
            slot = fd;
            ESP_LOGI(TAG, "client connected: %d", fd);
            return;
        }
    }
    ESP_LOGW(TAG, "client table full, fd %d not tracked", fd);
}

void CaiyuWebServer::RemoveClient(int fd) {
    for (int& slot : clients_) {
        if (slot == fd) {
            slot = 0;
            ESP_LOGI(TAG, "client closed: %d", fd);
            return;
        }
    }
}

bool CaiyuWebServer::HasClients() const {
    for (int fd : clients_) {
        if (fd != 0) {
            return true;
        }
    }
    return false;
}

void CaiyuWebServer::SyncChatState() {
    const DeviceState state = Application::GetInstance().GetDeviceState();
    if (state == last_chat_state_) {
        return;
    }
    last_chat_state_ = state;
    ESP_LOGI(TAG, "device state: %s", DeviceStateMachine::GetStateName(state));
    BroadcastChatState();
}

void CaiyuWebServer::BroadcastChatState() {
    BroadcastJson(BuildChatStateMessage(last_chat_state_));
}

void CaiyuWebServer::BroadcastJson(const std::string& message) {
    if (server_ == nullptr) {
        return;
    }
    for (int fd : clients_) {
        if (fd == 0) {
            continue;
        }
        auto* job = static_cast<ChatBroadcastJob*>(malloc(sizeof(ChatBroadcastJob)));
        auto* payload = static_cast<char*>(malloc(message.size()));
        if (job == nullptr || payload == nullptr) {
            free(job);
            free(payload);
            continue;
        }
        memcpy(payload, message.c_str(), message.size());
        job->server = server_;
        job->fd = fd;
        job->payload = payload;
        job->len = message.size();
        if (httpd_queue_work(server_, ChatBroadcastJobFn, job) != ESP_OK) {
            free(payload);
            free(job);
        }
    }
}

esp_err_t CaiyuWebServer::WsHandler(httpd_req_t* req) {
    if (instance_ == nullptr) {
        return ESP_FAIL;
    }
    return instance_->OnWebSocket(req);
}

esp_err_t CaiyuWebServer::OnWebSocket(httpd_req_t* req) {
    const int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        AddClient(fd);
        if (display_ != nullptr) {
            display_->HideQrCode();
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        RemoveClient(fd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        RemoveClient(fd);
        return ESP_OK;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 || frame.len > kMaxMessageLen) {
        ESP_LOGW(TAG, "drop frame: type=%d len=%u", frame.type, static_cast<unsigned>(frame.len));
        return ESP_OK;
    }

    uint8_t payload[kMaxMessageLen + 1];
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        return err;
    }
    payload[frame.len] = '\0';
    HandleMessage(req, reinterpret_cast<const char*>(payload), frame.len);
    return ESP_OK;
}

void CaiyuWebServer::HandleMessage(httpd_req_t* req, const char* data, size_t len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if (root == nullptr) {
        ESP_LOGW(TAG, "invalid json: %s", data);
        return;
    }

    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "hello") == 0) {
        SendText(req, HelloMessage());
    } else if (strcmp(type->valuestring, "emotion") == 0) {
        HandleEmotion(req, root);
    } else if (strcmp(type->valuestring, "lamp") == 0) {
        HandleLamp(req, root);
    } else if (strcmp(type->valuestring, "action") == 0) {
        HandleAction(req, root);
    } else if (strcmp(type->valuestring, "drive") == 0) {
        HandleDrive(req, root);
    } else if (strcmp(type->valuestring, "chat") == 0) {
        HandleChat(req, root);
    } else if (strcmp(type->valuestring, "trim") == 0) {
        HandleTrim(req, root);
    } else if (strcmp(type->valuestring, "speed") == 0) {
        HandleSpeed(req, root);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        const cJSON* stamp = cJSON_GetObjectItem(root, "t");
        cJSON* pong = cJSON_CreateObject();
        cJSON_AddStringToObject(pong, "type", "pong");
        if (stamp != nullptr) {
            cJSON_AddItemToObject(pong, "t", cJSON_Duplicate(stamp, true));
        }
        char* text = cJSON_PrintUnformatted(pong);
        if (text != nullptr) {
            SendText(req, text);
            cJSON_free(text);
        }
        cJSON_Delete(pong);
    } else {
        ESP_LOGW(TAG, "unknown message type: %s", type->valuestring);
    }

    cJSON_Delete(root);
}

void CaiyuWebServer::HandleEmotion(httpd_req_t* req, const cJSON* root) {
    const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
    if (!cJSON_IsString(emotion) || display_ == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "preview emotion: %s", emotion->valuestring);
    display_->SetPreviewEmotion(emotion->valuestring);

    cJSON* state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "state");
    cJSON_AddStringToObject(state, "emotion", emotion->valuestring);
    cJSON_AddBoolToObject(state, "preview", strcmp(emotion->valuestring, "auto") != 0);
    char* text = cJSON_PrintUnformatted(state);
    if (text != nullptr) {
        SendText(req, text);
        cJSON_free(text);
    }
    cJSON_Delete(state);
}

void CaiyuWebServer::HandleLamp(httpd_req_t* req, const cJSON* root) {
    if (strip_ == nullptr) {
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) {
        return;
    }

    const uint8_t r = JsonByte(root, "r", 255);
    const uint8_t g = JsonByte(root, "g", 255);
    const uint8_t b = JsonByte(root, "b", 255);
    const char* name = action->valuestring;

    if (strcmp(name, "on") == 0) {
        strip_->TurnOn(r, g, b);
    } else if (strcmp(name, "off") == 0) {
        strip_->TurnOff();
    } else if (strcmp(name, "breathe") == 0) {
        strip_->SetBreathe();
    } else if (strcmp(name, "rainbow") == 0) {
        strip_->SetRainbow();
    } else if (strcmp(name, "flow") == 0) {
        strip_->TurnOn(r, g, b);
        strip_->SetFlow();
    } else if (strcmp(name, "flash") == 0) {
        strip_->TurnOn(r, g, b);
        strip_->SetFlash();
    } else if (strcmp(name, "brightness") == 0) {
        strip_->SetBrightness(JsonByte(root, "value", strip_->Brightness()));
    } else if (strcmp(name, "speed") == 0) {
        strip_->SetSpeedPercent(JsonByte(root, "value", strip_->speed_percent()));
    } else if (strcmp(name, "query") != 0) {
        ESP_LOGW(TAG, "unknown lamp action: %s", name);
        return;
    }

    ESP_LOGI(TAG, "lamp: %s rgb=(%d,%d,%d)", name, r, g, b);
    SendLampState(req);
}

void CaiyuWebServer::SendLampState(httpd_req_t* req) {
    if (strip_ == nullptr) {
        return;
    }
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    strip_->GetColor(r, g, b);

    cJSON* state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "lamp");
    cJSON_AddBoolToObject(state, "on", strip_->IsOn());
    cJSON_AddNumberToObject(state, "mode", strip_->Mode());
    cJSON_AddNumberToObject(state, "brightness", strip_->Brightness());
    cJSON_AddNumberToObject(state, "speed", strip_->speed_percent());
    cJSON_AddNumberToObject(state, "r", r);
    cJSON_AddNumberToObject(state, "g", g);
    cJSON_AddNumberToObject(state, "b", b);
    char* text = cJSON_PrintUnformatted(state);
    if (text != nullptr) {
        SendText(req, text);
        cJSON_free(text);
    }
    cJSON_Delete(state);
}

void CaiyuWebServer::HandleAction(httpd_req_t* req, const cJSON* root) {
    if (actionments_ == nullptr) {
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) {
        return;
    }

    caiyu::Actionments::Action target = caiyu::Actionments::Action::kCount;
    if (strcmp(action->valuestring, "query") != 0) {
        if (!caiyu::Actionments::ActionFromName(action->valuestring, &target)) {
            ESP_LOGW(TAG, "unknown dog action: %s", action->valuestring);
            return;
        }
        actionments_->Request(target);
        ESP_LOGI(TAG, "dog action: %s", caiyu::Actionments::ActionName(target));
    }
    SendActionState(req);
}

void CaiyuWebServer::SendActionState(httpd_req_t* req) {
    if (actionments_ == nullptr) {
        return;
    }
    cJSON* state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "action");
    cJSON_AddStringToObject(state, "current",
                            caiyu::Actionments::ActionName(actionments_->current()));
    cJSON_AddBoolToObject(state, "busy", actionments_->busy());
    char* text = cJSON_PrintUnformatted(state);
    if (text != nullptr) {
        SendText(req, text);
        cJSON_free(text);
    }
    cJSON_Delete(state);
}

void CaiyuWebServer::HandleDrive(httpd_req_t* req, const cJSON* root) {
    if (actionments_ == nullptr) {
        return;
    }
    const cJSON* dir = cJSON_GetObjectItem(root, "dir");
    if (!cJSON_IsString(dir)) {
        return;
    }

    caiyu::Actionments::DriveDirection direction = caiyu::Actionments::DriveDirection::kNone;
    if (!caiyu::Actionments::DriveFromName(dir->valuestring, &direction)) {
        ESP_LOGW(TAG, "unknown drive direction: %s", dir->valuestring);
        return;
    }
    actionments_->SetDrive(direction);
    SendDriveState(req);
}

void CaiyuWebServer::SendDriveState(httpd_req_t* req) {
    if (actionments_ == nullptr) {
        return;
    }
    cJSON* state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "drive");
    cJSON_AddStringToObject(state, "dir", caiyu::Actionments::DriveName(actionments_->drive()));
    char* text = cJSON_PrintUnformatted(state);
    if (text != nullptr) {
        SendText(req, text);
        cJSON_free(text);
    }
    cJSON_Delete(state);
}

void CaiyuWebServer::HandleChat(httpd_req_t* req, const cJSON* root) {
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action) && strcmp(action->valuestring, "toggle") == 0) {
        ESP_LOGI(TAG, "chat toggle from web");
        Application::GetInstance().ToggleChatState();
        return;
    }
    SendChatState(req);
}

void CaiyuWebServer::SendChatState(httpd_req_t* req) {
    SendText(req, BuildChatStateMessage(Application::GetInstance().GetDeviceState()));
}

void CaiyuWebServer::HandleTrim(httpd_req_t* req, const cJSON* root) {
    if (actionments_ == nullptr) {
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action) && strcmp(action->valuestring, "save") == 0) {
        actionments_->SaveTrims();
    } else if (cJSON_IsString(action) && strcmp(action->valuestring, "reset") == 0) {
        for (int i = 0; i < caiyu::kLegCount; ++i) {
            actionments_->SetLegTrim(i, 0);
        }
        ESP_LOGI(TAG, "servo trims reset");
    } else {
        const cJSON* index = cJSON_GetObjectItem(root, "index");
        const cJSON* value = cJSON_GetObjectItem(root, "value");
        if (cJSON_IsNumber(index) && cJSON_IsNumber(value)) {
            if (actionments_->SetLegTrim(index->valueint, value->valueint)) {
                ESP_LOGI(TAG, "servo trim %d = %d", index->valueint, value->valueint);
            }
        }
    }
    SendTrimState(req);
}

void CaiyuWebServer::SendTrimState(httpd_req_t* req) {
    if (actionments_ == nullptr) {
        return;
    }
    int trims[caiyu::kLegCount];
    for (int i = 0; i < caiyu::kLegCount; ++i) {
        trims[i] = actionments_->leg_trim(i);
    }
    SendText(req, BuildTrimMessage(trims, caiyu::Actionments::kMaxTrimDeg,
                                   actionments_->trims_dirty()));
}

void CaiyuWebServer::HandleSpeed(httpd_req_t* req, const cJSON* root) {
    if (actionments_ == nullptr) {
        return;
    }
    const cJSON* value = cJSON_GetObjectItem(root, "value");
    if (cJSON_IsNumber(value)) {
        actionments_->SetSpeedPercent(value->valueint);
        ESP_LOGI(TAG, "action speed: %d%%", actionments_->speed_percent());
    }
    SendSpeedState(req);
}

void CaiyuWebServer::SendSpeedState(httpd_req_t* req) {
    if (actionments_ == nullptr) {
        return;
    }
    cJSON* state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "speed");
    cJSON_AddNumberToObject(state, "value", actionments_->speed_percent());
    char* text = cJSON_PrintUnformatted(state);
    if (text != nullptr) {
        SendText(req, text);
        cJSON_free(text);
    }
    cJSON_Delete(state);
}

void CaiyuWebServer::SendText(httpd_req_t* req, const std::string& message) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(message.c_str()));
    frame.len = message.size();
    frame.final = true;
    const esp_err_t err = httpd_ws_send_frame(req, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
    }
}
