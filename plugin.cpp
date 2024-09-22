// CLAP instrument plugin tutorial
//
// Slightly adapted from: https://nakst.gitlab.io/tutorial/clap-part-1.html
//
// Adjusted for C++20 by John Novak <john@johnnovak.net>
// https://github.com/johnnovak/

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "clap/clap.h"

// Parameters
constexpr auto ParamVolume = 0;
constexpr auto NumParams   = 1;

struct Voice {
    bool held       = false;
    int32_t note_id = 0;
    int16_t channel = 0;
    int16_t key     = 0;
    float phase     = 0.0f;

    float param_offsets[NumParams] = {};
};

struct MyPlugin {
    clap_plugin_t plugin    = {};
    const clap_host_t* host = nullptr;
    float sample_rate       = 48'000.0f;

    std::vector<Voice> voices = {};

    // for the audio thread
    float audio_params[NumParams]        = {};
    bool audio_params_changed[NumParams] = {};

    // for the main thread
    float main_params[NumParams]        = {};
    bool main_params_changed[NumParams] = {};

	std::mutex sync_params = {};
};

static void process_event(MyPlugin* plugin, const clap_event_header_t* event)
{
	if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {

		if (event->type == CLAP_EVENT_NOTE_ON ||
		    event->type == CLAP_EVENT_NOTE_OFF ||
		    event->type == CLAP_EVENT_NOTE_CHOKE) {

			const auto note_event = (const clap_event_note_t*)event;

			// Look through our voices array, and if the event
			// matches any of them, it must have been released.
			for (size_t i = 0; i < plugin->voices.size(); ++i) {
				auto voice = &plugin->voices[i];

				if ((note_event->key     == -1 || voice->key     == note_event->key) &&
				    (note_event->note_id == -1 || voice->note_id == note_event->note_id) &&
				    (note_event->channel == -1 || voice->channel == note_event->channel)) {

					if (event->type == CLAP_EVENT_NOTE_CHOKE) {
						// Stop the voice immediately;
						// don't process the release
						// segment of any ADSR envelopes.
						plugin->voices.erase(plugin->voices.cbegin() + i);
						--i;
					} else {
						voice->held = false;
					}
				}
			}

			// If this is a note on event, create a new voice and
			// add it to our vector.
			if (event->type == CLAP_EVENT_NOTE_ON) {
				Voice voice = {
				    .held    = true,
				    .note_id = note_event->note_id,
				    .channel = note_event->channel,
				    .key     = note_event->key,
				    .phase   = 0.0f,
				};

				plugin->voices.emplace_back(voice);
			}

		} else if (event->type == CLAP_EVENT_PARAM_VALUE) {
			const auto value_event = (const clap_event_param_value_t*)event;
			uint32_t i = (uint32_t)value_event->param_id;

			std::lock_guard lock(plugin->sync_params);

			plugin->audio_params[i]         = value_event->value;
			plugin->audio_params_changed[i] = true;


		} else if (event->type == CLAP_EVENT_PARAM_MOD) {
			const auto mod_event = (const clap_event_param_mod_t*)event;

			for (size_t i = 0; i < plugin->voices.size(); ++i) {
				auto voice = &plugin->voices[i];

				if ((mod_event->key     == -1 || voice->key     == mod_event->key) &&
				    (mod_event->note_id == -1 || voice->note_id == mod_event->note_id) &&
				    (mod_event->channel == -1 || voice->channel == mod_event->channel)) {

					voice->param_offsets[mod_event->param_id] = mod_event->amount;
					break;
				}
			}
		}
	}
}

static void render_audio(MyPlugin* plugin, uint32_t start, uint32_t end,
                         float* out_left, float* out_right)
{
	for (uint32_t index = start; index < end; ++index) {
		auto sum = 0.0f;

		for (size_t i = 0; i < plugin->voices.size(); ++i) {
			auto voice = &plugin->voices[i];
			if (!voice->held) {
				continue;
			}

			const auto volume = std::clamp(plugin->audio_params[ParamVolume] +
			                                   voice->param_offsets[ParamVolume],
			                               0.0f,
			                               1.0f);

			sum += sinf(voice->phase * 2.0f * 3.14159f) * 0.2f * volume;

			voice->phase += 440.0f *
			                exp2f((voice->key - 57.0f) / 12.0f) /
			                plugin->sample_rate;

			voice->phase -= floorf(voice->phase);
		}

		out_left[index]  = sum;
		out_right[index] = sum;
	}
}


static void sync_main_params_to_audio(MyPlugin* plugin,
                                      const clap_output_events_t* out)
{
	std::lock_guard lock(plugin->sync_params);

	for (uint32_t i = 0; i < NumParams; ++i) {
		if (plugin->main_params_changed[i]) {
			plugin->audio_params[i]        = plugin->main_params[i];
			plugin->main_params_changed[i] = false;

			clap_event_param_value_t event = {};

			event.header.size     = sizeof(event);
			event.header.time     = 0;
			event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
			event.header.type     = CLAP_EVENT_PARAM_VALUE;
			event.header.flags    = 0;
			event.param_id        = i;
			event.cookie          = NULL;
			event.note_id         = -1;
			event.port_index      = -1;
			event.channel         = -1;
			event.key             = -1;
			event.value           = plugin->audio_params[i];

			out->try_push(out, &event.header);
		}
	}
}

static bool sync_audio_params_to_main(MyPlugin* plugin)
{
	bool any_changed = false;

	std::lock_guard lock(plugin->sync_params);

	for (uint32_t i = 0; i < NumParams; ++i) {
		if (plugin->audio_params_changed[i]) {
			plugin->main_params[i] = plugin->audio_params[i];
			plugin->audio_params_changed[i] = false;

			any_changed = true;
		}
	}

	return any_changed;
}

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

    .features = (const char*[]){
		CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
		CLAP_PLUGIN_FEATURE_STEREO, NULL,
    },
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
		return NumParams;
	},

    .get_info = [](const clap_plugin_t* _plugin, uint32_t index,
                   clap_param_info_t* info) -> bool {
	    if (index == ParamVolume) {
		    memset(info, 0, sizeof(clap_param_info_t));

		    info->id = index;

		    // These flags enable polyphonic modulation.
		    info->flags = CLAP_PARAM_IS_AUTOMATABLE |
		                  CLAP_PARAM_IS_MODULATABLE |
		                  CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID;

		    info->min_value     = 0.0f;
		    info->max_value     = 1.0f;
		    info->default_value = 0.5f;

		    strcpy(info->name, "Volume");

		    return true;
	    } else {
		    return false;
	    }
    },

    .get_value = [](const clap_plugin_t* _plugin, clap_id id, double* value) -> bool {
	    auto plugin = (MyPlugin*)_plugin->plugin_data;

	    uint32_t i = (uint32_t)id;
	    if (i >= NumParams) {
		    return false;
	    }

	    // get_value is called on the main thread, but should return the
	    // value of the parameter according to the audio thread, since the
	    // value on the audio thread is the one that host communicates with
	    // us via CLAP_EVENT_PARAM_VALUE events. Since we're accessing the
	    // opposite thread's arrays, we must acquire the syncParameters
	    // mutex. And although we need to check the mainChanged array, we
	    // mustn't actually modify the parameters array, since that can only
	    // be done on the audio thread. Don't worry -- it'll pick up the
	    // changes eventually.
		std::lock_guard lock(plugin->sync_params);

		*value = plugin->main_params_changed[i] ? plugin->main_params[i]
												: plugin->audio_params[i];

		return true;
    },

    .value_to_text = [](const clap_plugin_t* _plugin, clap_id id, double value,
           				char* display, uint32_t size) {
		uint32_t i = (uint32_t)id;
		if (i >= NumParams) {
			return false;
		}

		snprintf(display, size, "%f", value);

		return true;
	},

    .text_to_value = [](const clap_plugin_t* _plugin, clap_id param_id,
			            const char* display, double* value) {
		// TODO Implement this.
		return false;
	},

    .flush = [](const clap_plugin_t* _plugin, const clap_input_events_t* in,
                const clap_output_events_t* out) {

		auto plugin = (MyPlugin*)_plugin->plugin_data;

		const uint32_t num_events = in->size(in);

		// For parameters that have been modified by the main thread,
		// send CLAP_EVENT_PARAM_VALUE events to the host.
		sync_main_params_to_audio(plugin, out);

		// Process events sent to our plugin from the host.
		for (uint32_t event_index = 0; event_index < num_events; ++event_index) {
			process_event(plugin, in->get(in, event_index));
		}
	},
};

static const clap_plugin_state_t extension_state = {
    .save = [](const clap_plugin_t* _plugin, const clap_ostream_t* stream) -> bool {
	    auto plugin = (MyPlugin*)_plugin->plugin_data;

	    // Synchronize any changes from the audio thread (that is, parameter
	    // values sent to us by the host) before we save the state of the
	    // plugin.
	    sync_audio_params_to_main(plugin);

	    const auto bytes_to_write = sizeof(float) * NumParams;

	    const auto bytes_written = stream->write(stream,
	                                             plugin->main_params,
	                                             bytes_to_write);

	    return (bytes_written == bytes_to_write);
    },

    .load = [](const clap_plugin_t* _plugin, const clap_istream_t* stream) -> bool {
	    auto plugin = (MyPlugin*)_plugin->plugin_data;

	    // Since we're modifying a parameter array, we need to acquire the
	    // sync_params mutex.
	    std::lock_guard lock(plugin->sync_params);

	    const auto bytes_to_read = sizeof(float) * NumParams;

	    const auto bytes_read = stream->read(stream,
	                                         plugin->main_params,
	                                         bytes_to_read);

	    const bool success = (bytes_read == bytes_to_read);

	    // Make sure that the audio thread will pick up upon the modified
	    // parameters next time pluginClass.process is called.
	    for (uint32_t i = 0; i < NumParams; ++i) {
		    plugin->main_params_changed[i] = true;
	    }

	    return success;
    },
};

//////////////////////////////////////////////////////////////////////////////
// Plugin class
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_t plugin_class = {

    .desc        = &plugin_descriptor,
    .plugin_data = nullptr,

    .init = [](const clap_plugin* _plugin) -> bool {
	    auto plugin = (MyPlugin*)_plugin->plugin_data;
		(void) plugin;

		for (uint32_t i = 0; i < NumParams; ++i) {
			clap_param_info_t info = {};

			extension_params.get_info(_plugin, i, &info);

			plugin->main_params[i]  = info.default_value;
			plugin->audio_params[i] = info.default_value;
		}

	    return true;
    },

    .destroy = [](const clap_plugin* _plugin) {
		auto plugin = (MyPlugin*)_plugin->plugin_data;

		std::lock_guard lock(plugin->sync_params);

		free(plugin);
	},

    .activate = [](const clap_plugin* _plugin, double sample_rate,
                   uint32_t min_frame_count, uint32_t max_frame_count) -> bool {

	    auto plugin = (MyPlugin*)_plugin->plugin_data;
	    plugin->sample_rate = sample_rate;
	    return true;
    },

    .deactivate = [](const clap_plugin* _plugin) {},

    .start_processing = [](const clap_plugin* _plugin) -> bool {
		return true;
	},

    .stop_processing = [](const clap_plugin* _plugin) {},

    .reset = [](const clap_plugin* _plugin) {
		// nop
    },

    .process = [](const clap_plugin* _plugin,
                  const clap_process_t* process) -> clap_process_status {

	    auto plugin = (MyPlugin*)_plugin->plugin_data;

	    assert(process->audio_outputs_count == 1);
	    assert(process->audio_inputs_count == 0);

	    const uint32_t frame_count       = process->frames_count;
	    const uint32_t input_event_count = process->in_events->size(process->in_events);

	    uint32_t event_index      = 0;
	    uint32_t next_event_frame = input_event_count ? 0 : frame_count;

		sync_main_params_to_audio(plugin, process->out_events);

	    for (uint32_t i = 0; i < frame_count;) {
		    while (event_index < input_event_count && next_event_frame == i) {
			    const clap_event_header_t* event =
			        process->in_events->get(process->in_events, event_index);

			    if (event->time != i) {
				    next_event_frame = event->time;
				    break;
			    }

			    process_event(plugin, event);
			    ++event_index;

			    if (event_index == input_event_count) {
				    next_event_frame = frame_count;
				    break;
			    }
		    }

		    render_audio(plugin,
		                 i,
		                 next_event_frame,
		                 process->audio_outputs[0].data32[0],
		                 process->audio_outputs[0].data32[1]);

		    i = next_event_frame;
	    }

	    for (size_t i = 0; i < plugin->voices.size(); ++i) {
		    auto voice = &plugin->voices[i];

		    if (!voice->held) {
			    clap_event_note_t event = {
			        .header = {
						.size     = sizeof(event),
						.time     = 0,
						.space_id = CLAP_CORE_EVENT_SPACE_ID,
						.type     = CLAP_EVENT_NOTE_END,
						.flags    = 0
					},
			        .key        = voice->key,
			        .note_id    = voice->note_id,
			        .channel    = voice->channel,
			        .port_index = 0
				};

			    process->out_events->try_push(process->out_events,
			                                  &event.header);

			    plugin->voices.erase(plugin->voices.cbegin() + i);
		    }
	    }

	    return CLAP_PROCESS_CONTINUE;
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

    .on_main_thread = [](const clap_plugin* _plugin) {},
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

	    auto plugin = new MyPlugin();

	    plugin->host               = host;
	    plugin->plugin             = plugin_class;
	    plugin->plugin.plugin_data = plugin;

	    return &plugin->plugin;
    },
};

//////////////////////////////////////////////////////////////////////////////
// Plugin definition
//////////////////////////////////////////////////////////////////////////////

extern "C" const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,

    .init = [](const char* path) -> bool {
		return true;
	},

    .deinit = []() {},

    .get_factory = [](const char* factory_id) -> const void* {
	    // Return a pointer to our plugin factory definition.
	    return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) ? nullptr
	                                                      : &plugin_factory;
    },
};

