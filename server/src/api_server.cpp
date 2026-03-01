#include "api_server.h"

#include <thread>

#ifdef HAVE_WORKFLOW
#include <workflow/WFHttpServer.h>
#endif

namespace avsvc {

namespace {
std::string extract_json_value(const std::string& body, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    const auto key_pos = body.find(token);
    if (key_pos == std::string::npos) {
        return "";
    }
    const auto colon_pos = body.find(':', key_pos + token.size());
    if (colon_pos == std::string::npos) {
        return "";
    }
    const auto first_quote = body.find('"', colon_pos + 1);
    if (first_quote == std::string::npos) {
        return "";
    }
    const auto second_quote = body.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return "";
    }
    return body.substr(first_quote + 1, second_quote - first_quote - 1);
}


std::string extract_query_value(const std::string& uri, const std::string& key) {
    const auto qpos = uri.find('?');
    if (qpos == std::string::npos) {
        return "";
    }
    const std::string query = uri.substr(qpos + 1);
    const std::string prefix = key + "=";
    size_t start = 0;
    while (start <= query.size()) {
        const auto amp = query.find('&', start);
        const auto end = (amp == std::string::npos) ? query.size() : amp;
        const std::string kv = query.substr(start, end - start);
        if (kv.rfind(prefix, 0) == 0) {
            return kv.substr(prefix.size());
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return "";
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}
}  // namespace

ApiServer::ApiServer(TaskManager& manager, MergeWorker& worker, AsrWorker& asr_worker)
    : manager_(manager), worker_(worker), asr_worker_(asr_worker) {}

int ApiServer::start(unsigned short port) const {
#ifdef HAVE_WORKFLOW
    WFHttpServer server([this](WFHttpTask* task) {
        protocol::HttpRequest* req = task->get_req();
        protocol::HttpResponse* resp = task->get_resp();

        const std::string uri = req->get_request_uri();
        const std::string method = req->get_method();

        resp->add_header_pair("Content-Type", "application/json; charset=utf-8");

        if (method == "POST" && uri == "/api/v1/merge") {
            const void* body_ptr = nullptr;
            size_t body_len = 0;
            req->get_parsed_body(&body_ptr, &body_len);
            const std::string body(static_cast<const char*>(body_ptr), body_len);

            const std::string video = extract_json_value(body, "video_path");
            const std::string audio = extract_json_value(body, "audio_path");
            const std::string output = extract_json_value(body, "output_path");

            if (video.empty() || audio.empty() || output.empty()) {
                resp->set_status_code("400");
                resp->append_output_body("{\"error\":\"video_path/audio_path/output_path are required\"}");
                return;
            }

            bool reused = false;
            const std::string fp = build_fingerprint(video, audio, output);
            const std::string task_id = manager_.create_or_reuse(fp, video, audio, output, &reused);

            if (!reused) {
                manager_.update_status(task_id, TaskStatus::Running, 0, "worker started");
                std::thread([this, task_id, video, audio, output]() {
                    const int rc = worker_.run(video, audio, output, [this, task_id](int p, const std::string& msg) {
                        manager_.update_status(task_id, p >= 100 ? TaskStatus::Success : TaskStatus::Running, p, msg);
                    });
                    if (rc != 0) {
                        std::string err = "merge failed";
                        const auto rec = manager_.get_task(task_id);
                        if (rec.has_value() && !rec->message.empty()) {
                            err = rec->message;
                        }
                        manager_.update_status(task_id, TaskStatus::Failed, 0, err);
                    }
                }).detach();
            }

            resp->append_output_body("{\"task_id\":\"" + task_id + "\",\"reused\":" + (reused ? "true" : "false") + "}");
            return;
        }


        if (method == "POST" && uri == "/api/v1/asr") {
            const void* body_ptr = nullptr;
            size_t body_len = 0;
            req->get_parsed_body(&body_ptr, &body_len);
            const std::string body(static_cast<const char*>(body_ptr), body_len);

            const std::string audio = extract_json_value(body, "audio_path");
            const std::string subtitle = extract_json_value(body, "subtitle_path");
            const std::string model_dir = extract_json_value(body, "model_dir");
            const std::string model_name = extract_json_value(body, "model_name");
            std::string language = extract_json_value(body, "language");
            if (language.empty()) {
                language = "en";
            }

            if (audio.empty() || subtitle.empty() || model_dir.empty() || model_name.empty()) {
                resp->set_status_code("400");
                resp->append_output_body("{\"error\":\"audio_path/subtitle_path/model_dir/model_name are required\"}");
                return;
            }

            bool reused = false;
            const std::string fp = build_fingerprint(audio, model_dir + "/" + model_name, subtitle + "|" + language);
            const std::string task_id = manager_.create_or_reuse(fp, "", audio, subtitle, &reused);

            if (!reused) {
                manager_.update_status(task_id, TaskStatus::Running, 0, "asr worker started");
                std::thread([this, task_id, audio, subtitle, model_dir, model_name, language]() {
                    const int rc = asr_worker_.run(audio, subtitle, model_dir, model_name, language,
                        [this, task_id](int p, const std::string& msg) {
                            manager_.update_status(task_id, p >= 100 ? TaskStatus::Success : TaskStatus::Running, p, msg);
                        });
                    if (rc != 0) {
                        std::string err = "asr failed";
                        const auto rec = manager_.get_task(task_id);
                        if (rec.has_value() && !rec->message.empty()) {
                            err = rec->message;
                        }
                        manager_.update_status(task_id, TaskStatus::Failed, 0, err);
                    }
                }).detach();
            }

            resp->append_output_body("{\"task_id\":\"" + task_id + "\",\"reused\":" + (reused ? "true" : "false") + "}");
            return;
        }

        if (method == "GET" && uri.rfind("/api/v1/task", 0) == 0) {
            const std::string task_id = extract_query_value(uri, "task_id");
            if (task_id.empty()) {
                resp->set_status_code("400");
                resp->append_output_body("{\"error\":\"task_id is required\"}");
                return;
            }

            const auto rec = manager_.get_task(task_id);
            if (!rec.has_value()) {
                resp->set_status_code("404");
                resp->append_output_body("{\"error\":\"task not found\"}");
                return;
            }

            resp->append_output_body(
                "{\"task_id\":\"" + rec->task_id +
                "\",\"status\":\"" + to_string(rec->status) +
                "\",\"progress\":" + std::to_string(rec->progress) +
                ",\"message\":\"" + json_escape(rec->message) + "\"}");
            return;
        }

        if (method == "GET" && uri == "/healthz") {
            resp->append_output_body("{\"ok\":true}");
            return;
        }

        resp->set_status_code("404");
        resp->append_output_body("{\"error\":\"not found\"}");
    });

    if (server.start(port) == 0) {
        server.wait_finish();
        return 0;
    }
    return -1;
#else
    (void)port;
    return -1;
#endif
}

}  // namespace avsvc
