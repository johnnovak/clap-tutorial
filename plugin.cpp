// CLAP instrument plugin tutorial
//
// Adapted from: https://nakst.gitlab.io/tutorial/clap-part-1.html
//
// Adjusted for C++20 by John Novak <john@johnnovak.net>
// https://github.com/johnnovak/

#include "my_plugin.h"

//////////////////////////////////////////////////////////////////////////////
// Plugin descriptor
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_descriptor_t plugin_descriptor = {

    .clap_version = CLAP_VERSION_INIT,
    .id           = "nakst.HelloCLAP",
    .name         = "HelloCLAP",
    .vendor       = "nakst",
    .url          = "https://nakst.gitlab.io",
    .manual_url   = "https://nakst.gitlab.io",
    .support_url  = "https://nakst.gitlab.io",
    .version      = "1.0.0",
    .description  = "The best audio plugin ever.",

    .features = (const char*[]) {
        CLAP_PLUGIN_FEATURE_INSTRUMENT,
        CLAP_PLUGIN_FEATURE_SYNTHESIZER,
        CLAP_PLUGIN_FEATURE_STEREO,
        nullptr
    }
};

//////////////////////////////////////////////////////////////////////////////
// Extensions
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_note_ports_t extension_note_ports = {
    .count = [](const clap_plugin_t* plugin, bool is_input) -> uint32_t {
        return is_input ? 1 : 0;
    },

    .get = [](const clap_plugin_t* plugin, uint32_t index, bool is_input,
              clap_note_port_info_t* info) -> bool {

        if (!is_input || index) {
            return false;
        }

        info->id = 0;
        // TODO Also support the MIDI dialect.
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect  = CLAP_NOTE_DIALECT_CLAP;

        snprintf(info->name, sizeof(info->name), "%s", "Note Port");

        return true;
    },
};

static const clap_plugin_audio_ports_t extension_audio_ports = {
    .count = [](const clap_plugin_t* plugin, bool is_input) -> uint32_t {
        return is_input ? 0 : 1;
    },

    .get = [](const clap_plugin_t* plugin, uint32_t index, bool is_input,
              clap_audio_port_info_t* info) -> bool {
        if (is_input || index) {
            return false;
        }

        info->id            = 0;
        info->channel_count = 2;
        info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type     = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;

        snprintf(info->name, sizeof(info->name), "%s", "Audio Output");

        return true;
    },
};

static const clap_plugin_params_t extension_params = {

    .count = [](const clap_plugin_t* plugin) -> uint32_t {
        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->GetParamCount();
    },

    .get_info = [](const clap_plugin_t* plugin, uint32_t index,
                   clap_param_info_t* info) -> bool {

        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->GetParamInfo(index, info);
    },

    .get_value = [](const clap_plugin_t* plugin, clap_id param_id, double* value) -> bool {
        auto my_plugin = (MyPlugin*)plugin->plugin_data;

        if (auto v = my_plugin->GetParamValue(param_id)) {
            *value = *v;
            return true;
        } else {
            return false;
        }
    },

    .value_to_text = [](const clap_plugin_t* plugin, clap_id param_id, double value,
                       char* display, uint32_t size) -> bool {
        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->ParamValueToText(param_id, value, display, size);
    },

    .text_to_value =
        [](const clap_plugin_t* plugin, clap_id param_id, const char* display,
           double* value) -> bool {
            auto my_plugin = (MyPlugin*)plugin->plugin_data;

            if (auto v = my_plugin->ParamTextToValue(param_id, display)) {
                *value = *v;
                return true;
            } else {
                return false;
            }
        },

    .flush =
        [](const clap_plugin_t* plugin, const clap_input_events_t* in,
           const clap_output_events_t* out) {
            auto my_plugin = (MyPlugin*)plugin->plugin_data;
            my_plugin->Flush(in, out);
        }};

static const clap_plugin_state_t extension_state = {
    .save = [](const clap_plugin_t* plugin, const clap_ostream_t* stream) -> bool {

        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->SaveState(stream);
    },

    .load = [](const clap_plugin_t* plugin, const clap_istream_t* stream) -> bool {

        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->LoadState(stream);
    },
};

//////////////////////////////////////////////////////////////////////////////
// Plugin class
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_t my_plugin_class = {

    .desc        = &plugin_descriptor,
    .plugin_data = nullptr,

    .init = [](const clap_plugin* plugin) -> bool {
        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->Init(plugin);
    },

    .destroy =
        [](const clap_plugin* plugin) {
            auto my_plugin = (MyPlugin*)plugin->plugin_data;

            my_plugin->Shutdown();
            delete my_plugin;
        },

    .activate = [](const clap_plugin* plugin, double sample_rate,
                   uint32_t min_frame_count, uint32_t max_frame_count) -> bool {

        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
    },

    .deactivate = [](const clap_plugin* plugin) {},

    .start_processing = [](const clap_plugin* plugin) -> bool { return true; },

    .stop_processing = [](const clap_plugin* plugin) {},

    .reset =
        [](const clap_plugin* plugin) {
            // nop
        },

    .process = [](const clap_plugin* plugin,
                  const clap_process_t* process) -> clap_process_status {
        auto my_plugin = (MyPlugin*)plugin->plugin_data;
        return my_plugin->Process(process);
    },

    .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
        if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
            return &extension_note_ports;

        } else if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
            return &extension_audio_ports;

        } else if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
            return &extension_params;

        } else if (strcmp(id, CLAP_EXT_STATE) == 0) {
            return &extension_state;

        } else {
            return nullptr;
        }
    },

    .on_main_thread = [](const clap_plugin* plugin) {},
};

//////////////////////////////////////////////////////////////////////////////
// Plugin factory
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_factory_t plugin_factory = {

    .get_plugin_count = [](const clap_plugin_factory* factory) -> uint32_t {
        return 1;
    },

    .get_plugin_descriptor = [](const clap_plugin_factory* factory,
                                uint32_t index) -> const clap_plugin_descriptor_t* {
        // Return a pointer to our plugin descriptor definition.
        return index == 0 ? &plugin_descriptor : nullptr;
    },

    .create_plugin = [](const clap_plugin_factory* factory, const clap_host_t* host,
                        const char* plugin_id) -> const clap_plugin_t* {
        if (!clap_version_is_compatible(host->clap_version) ||
            strcmp(plugin_id, plugin_descriptor.id)) {
            return nullptr;
        }

        auto my_plugin = new MyPlugin(my_plugin_class, host);
        return my_plugin->GetPluginClass();
    },
};

//////////////////////////////////////////////////////////////////////////////
// Plugin definition
//////////////////////////////////////////////////////////////////////////////

extern "C" const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,

    .init = [](const char* path) -> bool { return true; },

    .deinit = []() {},

    .get_factory = [](const char* factory_id) -> const void* {
        // Return a pointer to our plugin factory definition.
        return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &plugin_factory;
    },
};

