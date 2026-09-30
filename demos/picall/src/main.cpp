// ============================================================================
//  picall - a headless, video-only Pexip Pulse caller
// ----------------------------------------------------------------------------
//
//  Joins a Pexip Infinity conference from the command line and sends one
//  camera as MAIN video.  No audio device is ever attached and nothing is
//  rendered, so it runs fine on a Raspberry Pi with no screen or sound card.
//
//      1.  pulse_new()                           -> create the instance.
//      2.  pulse_options_set_*()                 -> callbacks + NULL windows.
//      3.  pulse_device_iterator_*()             -> find the camera.
//          pulse_device_session_connect_device() -> bind it to MAIN video.
//      4.  pulse_connect_with_rest_async()       -> join the conference.
//      5.  wait for Ctrl-C / far-end hang-up.
//      6.  pulse_disconnect() + pulse_free().
//
//  Usage:  picall <alias@server> [--pin PIN] [--name NAME] [--camera TEXT]
//          picall --list-devices
// ============================================================================

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <unistd.h>

#include <pexpulse/pulse.h>

namespace {

struct Options
{
    std::string alias;           // full "conference@server" as typed
    std::string server;          // part after the last '@'
    std::string pin;
    std::string display_name = "picall";
    std::string camera_filter;   // substring of the camera name, optional
    // The arm64 Pulse build's OpenSSL looks for CAs in a build-machine path,
    // so point it at the distro bundle.
    std::string ca_bundle    = "/etc/ssl/certs/ca-certificates.crt";
    bool        list_devices = false;
    bool        verbose      = false;
};

struct AppState
{
    Pulse *          pulse = nullptr;
    std::atomic<int> status{PULSE_CONNECTION_STATUS_DISCONNECTED};
    std::atomic<bool> was_connected{false};
    std::atomic<bool> connect_failed{false};
};

volatile std::sig_atomic_t g_stop = 0;
std::atomic<bool>          g_verbose{false};

void on_signal(int) { g_stop = 1; }

const char * status_to_string(int status)
{
    switch (status) {
        case PULSE_CONNECTION_STATUS_DISCONNECTED:  return "disconnected";
        case PULSE_CONNECTION_STATUS_CONNECTING:    return "connecting";
        case PULSE_CONNECTION_STATUS_RECONNECTING:  return "reconnecting";
        case PULSE_CONNECTION_STATUS_CONNECTED:     return "connected";
        case PULSE_CONNECTION_STATUS_DISCONNECTING: return "disconnecting";
        default:                                    return "unknown";
    }
}

// ----------------------------------------------------------------------------
//  Pulse callbacks (run on Pulse's own threads - keep them short)
// ----------------------------------------------------------------------------

void on_conference_status(const PulseConferenceStatusInfo * info, void * user_context)
{
    auto * app = static_cast<AppState *>(user_context);
    app->status.store(static_cast<int>(info->status));
    if (info->status == PULSE_CONNECTION_STATUS_CONNECTED)
        app->was_connected.store(true);
    std::fprintf(stderr, "picall: conference %s\n", status_to_string(info->status));
}

void on_connect_result(const PulseError err, void * user_context)
{
    auto * app = static_cast<AppState *>(user_context);
    if (err == PULSE_SUCCESS) {
        std::fprintf(stderr, "picall: joined the conference\n");
    } else {
        std::fprintf(stderr, "picall: connect failed: %s\n", pulse_strerror(err));
        app->connect_failed.store(true);
    }
}

void on_progress(const PulseOperationProgressInfo * info, void *)
{
    std::fprintf(stderr, "picall: [%3d%%] %s\n",
                 static_cast<int>(info->progress * 100.0f),
                 info->desc ? info->desc : "");
}

void on_pulse_log(void *, PulseDebugLevel level, const char * category,
                  int64_t, int64_t, unsigned int, const char *, const char *,
                  int, const char *, const char * message)
{
    if (!g_verbose.load() && level > PULSE_LEVEL_WARNING) return;
    std::fprintf(stderr, "[pulse:%s] %s\n",
                 category ? category : "?", message ? message : "");
}

// ----------------------------------------------------------------------------
//  Command line
// ----------------------------------------------------------------------------

void print_usage(const char * argv0)
{
    std::fprintf(stderr,
        "Usage: %s <alias@server> [options]\n"
        "       %s --list-devices\n"
        "\n"
        "Joins a Pexip Infinity conference and sends camera video only.\n"
        "\n"
        "Options:\n"
        "  --pin PIN        conference PIN (or set PICALL_PIN in the environment)\n"
        "  --name NAME      display name shown to others (default: picall)\n"
        "  --camera TEXT    use the first camera whose name contains TEXT\n"
        "  --ca-bundle PATH CA certificates file for TLS\n"
        "                   (default: /etc/ssl/certs/ca-certificates.crt)\n"
        "  --list-devices   print the cameras Pulse can see, then exit\n"
        "  -v, --verbose    print all Pulse log output\n"
        "  -h, --help       show this help\n",
        argv0, argv0);
}

// Returns false (after printing why) if the command line is unusable.
bool parse_args(int argc, char ** argv, Options & opt)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char * name) -> const char * {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "picall: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--pin") {
            const char * v = next_value("--pin");
            if (!v) return false;
            opt.pin = v;
        } else if (arg == "--name") {
            const char * v = next_value("--name");
            if (!v) return false;
            opt.display_name = v;
        } else if (arg == "--camera") {
            const char * v = next_value("--camera");
            if (!v) return false;
            opt.camera_filter = v;
        } else if (arg == "--ca-bundle") {
            const char * v = next_value("--ca-bundle");
            if (!v) return false;
            opt.ca_bundle = v;
        } else if (arg == "--list-devices") {
            opt.list_devices = true;
        } else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "picall: unknown option %s\n", arg.c_str());
            return false;
        } else if (opt.alias.empty()) {
            opt.alias = arg;
        } else {
            std::fprintf(stderr, "picall: unexpected argument %s\n", arg.c_str());
            return false;
        }
    }

    if (opt.list_devices) return true;

    // Same split pexninja uses: server = after the last '@', and the
    // conference name keeps the full alias.
    const auto at = opt.alias.find_last_of('@');
    if (opt.alias.empty() || at == std::string::npos || at == 0
        || at + 1 == opt.alias.size()) {
        std::fprintf(stderr, "picall: expected <alias@server>, e.g. meet.alice@example.com\n");
        return false;
    }
    opt.server = opt.alias.substr(at + 1);

    if (opt.pin.empty()) {
        if (const char * env_pin = std::getenv("PICALL_PIN")) opt.pin = env_pin;
    }
    return true;
}

// ----------------------------------------------------------------------------
//  Camera
// ----------------------------------------------------------------------------

// Returns a copy (caller frees with pulse_device_free) of the chosen camera,
// or nullptr.  With `print` set, lists every camera on stdout.
PulseDevice * pick_camera(Pulse * pulse, const std::string & filter, bool print)
{
    PulseDeviceIterator * it = nullptr;
    pulse_device_iterator_new(pulse, PULSE_MEDIA_VIDEO, PULSE_MEDIA_INPUT, &it);
    if (!it) return nullptr;

    const PulseDevice * first    = nullptr;
    const PulseDevice * fallback = nullptr;
    const PulseDevice * match    = nullptr;
    for (const PulseDevice * d = pulse_device_iterator_first(it); d;
         d = pulse_device_iterator_next(it)) {
        const char * name = pulse_device_get_name(d);
        const bool is_default = pulse_device_is_system_default(d);
        if (print)
            std::printf("  %s%s\n", name ? name : "(unnamed)",
                        is_default ? "  [default]" : "");
        if (!first) first = d;
        if (is_default && !fallback) fallback = d;
        if (!match && !filter.empty() && name && std::strstr(name, filter.c_str()))
            match = d;
    }

    const PulseDevice * chosen = filter.empty() ? (fallback ? fallback : first) : match;
    PulseDevice * copy = chosen ? pulse_device_copy(chosen) : nullptr;
    pulse_device_iterator_free(it);
    return copy;
}

// Pulse fills its device list in the background, so give it a moment.
PulseDevice * wait_for_camera(Pulse * pulse, const std::string & filter)
{
    for (int attempt = 0; attempt < 30 && !g_stop; ++attempt) {
        if (PulseDevice * cam = pick_camera(pulse, filter, false)) return cam;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}

void print_no_camera_hint()
{
    std::fprintf(stderr,
        "picall: no usable camera found.\n"
        "  Pulse reads cameras through V4L2. A Raspberry Pi CSI camera is only\n"
        "  reachable through libcamera (the raw \"unicam\" node won't stream),\n"
        "  so run picall via libcamerify:\n"
        "      sudo apt install libcamera-v4l2\n"
        "      libcamerify ./build/run-picall.sh ...\n"
        "  Check what Pulse sees with --list-devices.\n");
}

} // namespace

int main(int argc, char ** argv)
{
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        print_usage(argv[0]);
        return 2;
    }
    g_verbose.store(opt.verbose);

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Must come before pulse_new() or the early start-up logs are lost.
    pulse_global_logger_callback(on_pulse_log, nullptr);

    AppState app;
    app.pulse = pulse_new();
    if (!app.pulse) {
        std::fprintf(stderr, "picall: pulse_new() returned NULL\n");
        return 1;
    }

    if (opt.list_devices) {
        if (PulseDevice * cam = wait_for_camera(app.pulse, ""))
            pulse_device_free(cam);
        std::printf("Cameras:\n");
        PulseDevice * listed = pick_camera(app.pulse, "", true);
        const bool found = listed != nullptr;
        if (found) {
            pulse_device_free(listed);
        } else {
            std::printf("  (none)\n");
            print_no_camera_hint();
        }
        pulse_free(app.pulse);
        return found ? 0 : 2;
    }

    PulseConferenceStatusCallbackConfig conf_cb{on_conference_status, &app};
    pulse_options_set_conference_state_callback(app.pulse, &conf_cb);
    pulse_options_set_application_user_agent_string(app.pulse, "picall/0.1");

    if (access(opt.ca_bundle.c_str(), R_OK) == 0) {
        PulseError ca_err = pulse_options_set_ca_bundle_path(app.pulse, opt.ca_bundle.c_str());
        if (ca_err != PULSE_SUCCESS)
            std::fprintf(stderr, "picall: could not use CA bundle %s: %s\n",
                         opt.ca_bundle.c_str(), pulse_strerror(ca_err));
    } else {
        std::fprintf(stderr, "picall: CA bundle %s not readable; TLS may fail\n",
                     opt.ca_bundle.c_str());
    }

    // Nothing is rendered: NULL handles stop Pulse from opening its own windows.
    pulse_options_set_self_view_window_handle         (app.pulse, nullptr);
    pulse_options_set_remote_video_window_handle      (app.pulse, nullptr);
    pulse_options_set_presentation_video_window_handle(app.pulse, nullptr);

    // Video in only.  No audio device session is ever connected.
    PulseDevice * camera = wait_for_camera(app.pulse, opt.camera_filter);
    if (!camera) {
        if (!opt.camera_filter.empty())
            std::fprintf(stderr, "picall: no camera name contains \"%s\"\n",
                         opt.camera_filter.c_str());
        print_no_camera_hint();
        pulse_options_set_conference_state_callback(app.pulse, nullptr);
        pulse_free(app.pulse);
        return 2;
    }
    std::fprintf(stderr, "picall: using camera \"%s\"\n", pulse_device_get_name(camera));
    PulseError err = pulse_device_session_connect_device(app.pulse, camera,
                                                         PULSE_MEDIA_CONTENT_MAIN);
    if (err != PULSE_SUCCESS) {
        std::fprintf(stderr, "picall: could not attach camera: %s\n", pulse_strerror(err));
        print_no_camera_hint();
        pulse_device_free(camera);
        pulse_options_set_conference_state_callback(app.pulse, nullptr);
        pulse_free(app.pulse);
        return 2;
    }

    PulseRestConnectionConfig cfg{};
    cfg.server_address  = opt.server.c_str();
    cfg.conference_name = opt.alias.c_str();
    cfg.display_name    = opt.display_name.c_str();
    cfg.pin_code        = opt.pin.empty() ? nullptr : opt.pin.c_str();

    PulseAsyncOperationResultCallbackConfig result_cb{on_connect_result, &app};
    PulseOperationProgressCallbackConfig    progress_cb{on_progress, &app};

    std::fprintf(stderr, "picall: calling %s via %s\n", opt.alias.c_str(), opt.server.c_str());
    err = pulse_connect_with_rest_async(app.pulse, &cfg, &result_cb, &progress_cb);
    if (err != PULSE_SUCCESS) {
        std::fprintf(stderr, "picall: pulse_connect_with_rest_async: %s\n", pulse_strerror(err));
        app.connect_failed.store(true);
    }

    // Stay in the call until Ctrl-C, a failed connect, or the far end hangs up.
    while (!g_stop && !app.connect_failed.load()) {
        if (app.was_connected.load()
            && app.status.load() == PULSE_CONNECTION_STATUS_DISCONNECTED) {
            std::fprintf(stderr, "picall: call ended by the far end\n");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    const int exit_code = app.connect_failed.load() ? 1 : 0;

    if (pulse_is_connected(app.pulse)) {
        std::fprintf(stderr, "picall: hanging up\n");
        pulse_disconnect(app.pulse, nullptr);
    }
    pulse_device_session_disconnect_main_video(app.pulse, PULSE_MEDIA_CONTENT_MAIN,
                                               PULSE_MEDIA_INPUT);
    pulse_device_free(camera);
    // Clear the callback before pulse_free() so it can't fire on a dead AppState.
    pulse_options_set_conference_state_callback(app.pulse, nullptr);
    pulse_free(app.pulse);
    return exit_code;
}
