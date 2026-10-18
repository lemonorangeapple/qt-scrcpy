#include "streamer.h"

#include <napi/native_api.h>

#include <string>

namespace {

napi_value MakeString(napi_env env, const std::string &text)
{
    napi_value value;
    napi_create_string_utf8(env, text.c_str(), text.size(), &value);
    return value;
}

// start(source: number, width: number, height: number, fps: number, bitrate: number, port: number): string
// Returns "" on success, otherwise the reason.
napi_value Start(napi_env env, napi_callback_info info)
{
    size_t argc = 6;
    napi_value args[6] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 6)
        return MakeString(env, "start() needs 6 arguments");

    int32_t values[6] = {0};
    for (int i = 0; i < 6; ++i) {
        if (napi_get_value_int32(env, args[i], &values[i]) != napi_ok)
            return MakeString(env, "start() arguments must be numbers");
    }
    if (values[0] < 0 || values[0] > 2)
        return MakeString(env, "unknown source");

    qsc::Options options;
    options.source = static_cast<qsc::Source>(values[0]);
    options.width = values[1];
    options.height = values[2];
    options.frameRate = values[3] > 0 ? values[3] : 30;
    options.bitrate = values[4] > 0 ? values[4] : 6'000'000;
    options.port = values[5] > 0 ? values[5] : 27183;

    std::string error;
    if (!qsc::Start(options, error))
        return MakeString(env, error.empty() ? "unknown error" : error);
    return MakeString(env, "");
}

napi_value Stop(napi_env env, napi_callback_info)
{
    qsc::Stop();
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

napi_value IsRunning(napi_env env, napi_callback_info)
{
    napi_value value;
    napi_get_boolean(env, qsc::IsRunning(), &value);
    return value;
}

napi_value LastError(napi_env env, napi_callback_info)
{
    return MakeString(env, qsc::LastError());
}

napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor properties[] = {
        {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isRunning", nullptr, IsRunning, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"lastError", nullptr, LastError, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties);
    return exports;
}

napi_module g_module = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "streamer",
    .nm_priv = nullptr,
    .reserved = {nullptr},
};

} // namespace

extern "C" __attribute__((constructor)) void RegisterStreamerModule()
{
    napi_module_register(&g_module);
}
