// CLAP instrument plugin tutorial
//
// Adapted from: https://nakst.gitlab.io/tutorial/clap-part-1.html
//
// Adjusted for C++20 by John Novak <john@johnnovak.net>
// https://github.com/johnnovak/

#include <mutex>
#include <optional>
#include <vector>

#include "clap/clap.h"

class MyPlugin {

public:
    // Init/shutdown
    MyPlugin(const clap_plugin_t _plugin_class, const clap_host_t* _host);
    const clap_plugin_t* GetPluginClass();

    bool Init(const clap_plugin* _plugin_instance);
    void Shutdown();

    bool Activate(const double _sample_rate, const uint32_t min_frame_count,
                  const uint32_t max_frame_count);

    // Processing
    clap_process_status Process(const clap_process_t* process);

    void Flush(const clap_input_events_t* in, const clap_output_events_t* out);

    // Parameters
    uint32_t GetParamCount();
    bool GetParamInfo(const uint32_t index, clap_param_info_t* info);
    std::optional<double> GetParamValue(const clap_id id);

    bool ParamValueToText(const clap_id id, const double value, char* display,
                          const uint32_t size);

    std::optional<double> ParamTextToValue(const clap_id id, const char* display);

    // State handling
    bool LoadState(const clap_istream_t* stream);
    bool SaveState(const clap_ostream_t* stream);

private:
    void ProcessEvent(const clap_event_header_t* event);

    void RenderAudio(const uint32_t start, const uint32_t end, float* out_left,
                     float* out_right);

    void SyncMainParamsToAudio(const clap_output_events_t* out);
    bool SyncAudioParamsToMain();

private:
    static constexpr auto ParamVolume = 0;
    static constexpr auto NumParams   = 1;

    struct Voice {
        bool held       = false;
        int32_t note_id = 0;
        int16_t channel = 0;
        int16_t key     = 0;
        float phase     = 0.0f;

        float param_offsets[NumParams] = {};
    };

    clap_plugin_t plugin_class         = {};
    const clap_host_t* host            = nullptr;
    const clap_plugin* plugin_instance = nullptr;

    float sample_rate = 48'000.0f;

    std::vector<Voice> voices = {};

    // for the audio thread
    float audio_params[NumParams]        = {};
    bool audio_params_changed[NumParams] = {};

    // for the main thread
    float main_params[NumParams]        = {};
    bool main_params_changed[NumParams] = {};

    std::mutex sync_params = {};
};
