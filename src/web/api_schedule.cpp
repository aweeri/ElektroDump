// ElektroDump — schedule REST endpoints (spec §3.7, milestone M8).
// GET /api/v1/schedule returns the schedule + state; PUT validates and applies
// it (200, or 400 with field errors).
#include <cstdint>
#include <string>
#include <vector>

#include "scheduler/scheduler.h"
#include "util/json_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

json::Value state_json(const scheduler::ScheduleState& st) {
    json::Value out;
    out.set("enabled", json::Value(st.enabled));
    out.set("active_entry", st.active_entry.empty()
                                ? json::Value(nullptr)
                                : json::Value(st.active_entry));
    out.set("next_entry", st.next_entry.empty() ? json::Value(nullptr)
                                                : json::Value(st.next_entry));
    out.set("next_transition_utc",
            st.next_transition_utc.empty()
                ? json::Value(nullptr)
                : json::Value(st.next_transition_utc));
    out.set("seconds_to_next",
            json::Value(static_cast<int64_t>(st.seconds_to_next)));
    return out;
}

json::Value fields_json(const std::vector<scheduler::FieldError>& fields) {
    json::Value::Array arr;
    arr.reserve(fields.size());
    for (const scheduler::FieldError& f : fields) {
        json::Value item;
        item.set("path", json::Value(f.path));
        item.set("message", json::Value(f.message));
        arr.push_back(std::move(item));
    }
    return json::Value(std::move(arr));
}

HttpResponse validation_error(const std::vector<scheduler::FieldError>& fields) {
    json::Value out;
    out.set("error", json::Value("validation_failed"));
    out.set("fields", fields_json(fields));
    return HttpResponse::json(out.dump(0), 400);
}

}  // namespace

void register_schedule_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/schedule", [&ctx](const HttpRequest&) {
        json::Value root;
        if (ctx.scheduler != nullptr) {
            root.set("schedule",
                     scheduler::schedule_to_json(ctx.scheduler->schedule()));
            root.set("state", state_json(ctx.scheduler->state()));
        } else {
            root.set("schedule", json::Value(nullptr));
            root.set("state", json::Value(nullptr));
        }
        return HttpResponse::json(root.dump(0));
    });

    router.put("/api/v1/schedule", [&ctx](const HttpRequest& req) {
        if (ctx.scheduler == nullptr) {
            return HttpResponse::error(503, "no_scheduler");
        }
        std::string parse_error;
        const json::Value body = json::Value::parse(req.body, &parse_error);
        if (!parse_error.empty() || !body.is_object()) {
            return HttpResponse::error(400, "invalid_json");
        }
        // Accept either {"schedule": {...}} or the schedule object directly.
        const json::Value* wrapped = body.find("schedule");
        const json::Value& sched_root =
            (wrapped != nullptr && wrapped->is_object()) ? *wrapped : body;

        std::vector<scheduler::FieldError> errors;
        const scheduler::Schedule parsed =
            scheduler::parse_schedule(sched_root, &errors);
        if (errors.empty()) {
            scheduler::validate_schedule(parsed, &errors);
        }
        if (!errors.empty()) {
            return validation_error(errors);
        }

        std::vector<scheduler::FieldError> apply_errors;
        if (!ctx.scheduler->apply(parsed, &apply_errors)) {
            return validation_error(apply_errors);
        }

        json::Value out;
        out.set("schedule",
                scheduler::schedule_to_json(ctx.scheduler->schedule()));
        out.set("state", state_json(ctx.scheduler->state()));
        return HttpResponse::json(out.dump(0), 200);
    });
}

}  // namespace ed::web
