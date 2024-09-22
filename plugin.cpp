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
#include <string>
#include <vector>

#include "clap/clap.h"

struct Voice {
	bool held       = false;
	int32_t note_id = 0;
	int16_t channel = 0;
	int16_t key     = 0;
	float phase     = 0.0f;
};

struct MyPlugin {
	clap_plugin_t plugin      = {};
	const clap_host_t* host   = nullptr;
	float sample_rate         = 48'000.0f;
	std::vector<Voice> voices = {};
};

static void process_event(MyPlugin* plugin, const clap_event_header_t* event)
{
	if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {
		if (event->type == CLAP_EVENT_NOTE_ON ||
		    event->type == CLAP_EVENT_NOTE_OFF ||
		    event->type == CLAP_EVENT_NOTE_CHOKE) {

			const clap_event_note_t* note_event = (const clap_event_note_t*)event;

			// Look through our voices array, and if the event
			// matches any of them, it must have been released.
			for (size_t i = 0; i < plugin->voices.size(); ++i) {
				Voice* voice = &plugin->voices[i];

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
		}
	}
}

static void render_audio(MyPlugin* plugin, uint32_t start, uint32_t end,
                         float* out_left, float* out_right)
{
	for (uint32_t index = start; index < end; ++index) {
		float sum = 0.0f;

		for (size_t i = 0; i < plugin->voices.size(); ++i) {
			Voice* voice = &plugin->voices[i];
			if (!voice->held) {
				continue;
			}
			sum += sinf(voice->phase * 2.0f * 3.14159f) * 0.2f;

			voice->phase += 440.0f *
			                exp2f((voice->key - 57.0f) / 12.0f) /
			                plugin->sample_rate;

			voice->phase -= floorf(voice->phase);
		}

		out_left[index]  = sum;
		out_right[index] = sum;
	}
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

//////////////////////////////////////////////////////////////////////////////
// Plugin class
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_t plugin_class = {

    .desc        = &plugin_descriptor,
    .plugin_data = nullptr,

    .init = [](const clap_plugin* _plugin) -> bool {
	    MyPlugin* plugin = (MyPlugin*)_plugin->plugin_data;
	    (void)plugin;
	    return true;
    },

    .destroy = [](const clap_plugin* _plugin) {
		MyPlugin* plugin = (MyPlugin*)_plugin->plugin_data;
		free(plugin);
	},

    .activate = [](const clap_plugin* _plugin, double sample_rate,
                   uint32_t min_frame_count, uint32_t max_frame_count) -> bool {

	    MyPlugin* plugin    = (MyPlugin*)_plugin->plugin_data;
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

	    MyPlugin* plugin = (MyPlugin*)_plugin->plugin_data;

	    assert(process->audio_outputs_count == 1);
	    assert(process->audio_inputs_count == 0);

	    const uint32_t frame_count       = process->frames_count;
	    const uint32_t input_event_count = process->in_events->size( process->in_events);

	    uint32_t event_index      = 0;
	    uint32_t next_event_frame = input_event_count ? 0 : frame_count;

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
		    Voice* voice = &plugin->voices[i];

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
	    if (0 == strcmp(id, CLAP_EXT_NOTE_PORTS)) {
		    return &extension_note_ports;
	    }
	    if (0 == strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
		    return &extension_audio_ports;
	    }
	    return nullptr;
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

	    // Allocate the plugin structure, and fill in the plugin
	    // information from the plugin class variable.
	    MyPlugin* plugin = (MyPlugin*)calloc(1, sizeof(MyPlugin));

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

