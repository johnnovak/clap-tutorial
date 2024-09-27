// CLAP instrument plugin tutorial
//
// Adapted from:
//   https://nakst.gitlab.io/tutorial/clap-part-1.html
//   https://nakst.gitlab.io/tutorial/clap-part-2.html
//
// Adjusted for C++20 by John Novak <john@johnnovak.net>
// https://github.com/johnnovak/

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "my_plugin.h"

// Unsigned-only integer division with ceiling
template<typename T1, typename T2>
inline constexpr T1 ceil_udivide(const T1 x, const T2 y) noexcept {
	static_assert(std::is_unsigned<T1>::value, "First parameter should be unsigned");
	static_assert(std::is_unsigned<T2>::value, "Second parameter should be unsigned");
	return (x != 0) ? 1 + ((x - 1) / y) : 0;
	// https://stackoverflow.com/a/2745086
}

MyPlugin::MyPlugin(const clap_plugin_t _plugin_class, const clap_host_t* _host,
                   const Waveform _waveform)
{
    plugin_class = _plugin_class;
    host         = _host;
    waveform     = _waveform;

    plugin_class.plugin_data = this;
}

const clap_plugin_t* MyPlugin::GetPluginClass()
{
    return &plugin_class;
}

bool MyPlugin::Init(const clap_plugin* _plugin_instance)
{
    plugin_instance = _plugin_instance;

    for (uint32_t i = 0; i < NumParams; ++i) {
        clap_param_info_t info = {};

        auto extension_params = reinterpret_cast<const clap_plugin_params_t*>(
            plugin_class.get_extension(plugin_instance, CLAP_EXT_PARAMS));

        extension_params->get_info(&plugin_class, i, &info);

        main_params[i]  = info.default_value;
        audio_params[i] = info.default_value;
    }

    return true;
}

void MyPlugin::Shutdown()
{
    std::lock_guard lock(sync_params);

	if (resampler) {
		speex_resampler_destroy(resampler);
		resampler = nullptr;
	}
}

bool MyPlugin::Activate(const double sample_rate, const uint32_t min_frame_count,
                        const uint32_t max_frame_count)
{
    output_sample_rate_hz = sample_rate;
	resample_ratio = RenderSampleRateHz / output_sample_rate_hz;

    const spx_uint32_t in_rate_hz  = static_cast<int>(RenderSampleRateHz);
    const spx_uint32_t out_rate_hz = static_cast<int>(output_sample_rate_hz);

    constexpr auto NumChannels     = 2; // always stereo
    constexpr auto ResampleQuality = SPEEX_RESAMPLER_QUALITY_DESKTOP;

    resampler = speex_resampler_init(
        NumChannels, in_rate_hz, out_rate_hz, ResampleQuality, nullptr);

    speex_resampler_set_rate(resampler, in_rate_hz, out_rate_hz);
	speex_resampler_reset_mem(resampler);
	speex_resampler_skip_zeros(resampler);

    const auto max_render_buf_size = static_cast<size_t>(
        static_cast<double>(max_frame_count) * resample_ratio * 1.10f);

    render_buf[0].resize(max_render_buf_size);
	render_buf[1].resize(max_render_buf_size);

    return true;
}

clap_process_status MyPlugin::Process(const clap_process_t* process)
{
    assert(process->audio_outputs_count == 1);
    assert(process->audio_inputs_count == 0);

    const uint32_t num_frames = process->frames_count;
    const uint32_t num_events = process->in_events->size(process->in_events);

    uint32_t event_index      = 0;
    uint32_t next_event_frame = (num_events == 0) ? num_frames : 0;

    SyncMainParamsToAudio(process->out_events);

    for (uint32_t curr_frame = 0; curr_frame < num_frames;) {
        while (event_index < num_events && next_event_frame == curr_frame) {

            const auto event = process->in_events->get(process->in_events,
                                                       event_index);
            if (event->time != curr_frame) {
                next_event_frame = event->time;
                break;
            }

            ProcessEvent(event);
            ++event_index;

            if (event_index == num_events) {
                // We've reached the end of the event list
                next_event_frame = num_frames;
                break;
            }
        }

        const auto num_frames_to_render = static_cast<int>(
            static_cast<double>(next_event_frame - curr_frame) * resample_ratio);

        // Render samples until the next event
        RenderAudio(num_frames_to_render);

        curr_frame = next_event_frame;
    }

    // Resample
    const auto input_len  = render_buf[0].size();
    const auto output_len = num_frames;

    spx_uint32_t in_len  = input_len;
    spx_uint32_t out_len = output_len;

    speex_resampler_process_float(resampler,
                                  0,
                                  render_buf[0].data(),
                                  &in_len,
                                  process->audio_outputs[0].data32[0],
                                  &out_len);

    in_len  = input_len;
    out_len = output_len;

    speex_resampler_process_float(resampler,
                                  1,
                                  render_buf[1].data(),
                                  &in_len,
                                  process->audio_outputs[0].data32[1],
                                  &out_len);

    // Speex returns the number actually consumed and written samples in
    // `in_len` and `out_len`, respectively. There are three outcomes:
    //
    // 1) The input buffer hasn't been fully consumed, but the output buffer
    //    has been completely filled.
    //
    // 2) The output buffer hasn't been filled completely, but all input
    //    samples have been consumed.
    //
    // 3) All input samples have been consumed and the output buffer has been
    //    completely filled.
    //
    if (out_len < output_len) {
        // Case 2: The output buffer hasn't been filled completely; we need to
        // generate more input samples.
        //
        const auto num_out_frames_remaining = output_len - out_len;
        const auto curr_out_pos             = out_len;

        // "It's the only way to be sure"
        const auto render_frame_count = static_cast<int>(std::ceil(
            static_cast<double>(num_out_frames_remaining) * resample_ratio));

        render_buf[0].clear();
        render_buf[1].clear();

        RenderAudio(render_frame_count);

        in_len  = render_buf[0].size();
        out_len = num_out_frames_remaining;

        speex_resampler_process_float(resampler,
                                      0,
                                      render_buf[0].data(),
                                      &in_len,
                                      process->audio_outputs[0].data32[0] + curr_out_pos,
                                      &out_len);

        in_len  = render_buf[1].size();
        out_len = num_out_frames_remaining;

        speex_resampler_process_float(resampler,
                                      1,
                                      render_buf[1].data(),
                                      &in_len,
                                      process->audio_outputs[0].data32[1] + curr_out_pos,
                                      &out_len);
    }

    if (in_len < input_len) {
        // Case 1: The input buffer hasn't been fully consumed; we have
        // leftover input samples that we need to keep for the next Process()
        // call.
        //
        if (in_len > 0) {
            render_buf[0].erase(render_buf[0].begin(), render_buf[0].begin() + in_len);
            render_buf[1].erase(render_buf[1].begin(), render_buf[1].begin() + in_len);
        }

    } else {
        // Case 3: All input samples have been consumed and the output buffer
        // has been completely filled.
        //
        render_buf[0].clear();
        render_buf[1].clear();
    }

    // Clear voices
    for (size_t i = 0; i < voices.size(); ++i) {
        auto voice = &voices[i];

        if (!voice->held) {
            clap_event_note_t event = {
                .header     = {.size     = sizeof(event),
                               .time     = 0,
                               .space_id = CLAP_CORE_EVENT_SPACE_ID,
                               .type     = CLAP_EVENT_NOTE_END,
                               .flags    = 0},
                .key        = voice->key,
                .note_id    = voice->note_id,
                .channel    = voice->channel,
                .port_index = 0
            };

            process->out_events->try_push(process->out_events, &event.header);

            voices.erase(voices.cbegin() + i);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

uint32_t MyPlugin::GetParamCount()
{
    return NumParams;
}

bool MyPlugin::GetParamInfo(const uint32_t index, clap_param_info_t* info)
{
    if (index == ParamVolume) {
        memset(info, 0, sizeof(clap_param_info_t));

        info->id = index;

        // These flags enable polyphonic modulation.
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE |
                      CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID;

        info->min_value     = 0.0f;
        info->max_value     = 1.0f;
        info->default_value = 0.5f;

        strcpy(info->name, "Volume");

        return true;
    } else {
        return false;
    }
}

std::optional<double> MyPlugin::GetParamValue(const clap_id id)
{
    uint32_t i = id;
    if (i >= NumParams) {
        return {};
    }

    // This gets called on the main thread, but should return the value of the
    // parameter according to the audio thread, since the value on the audio
    // thread is the one that host communicates with us via
    // CLAP_EVENT_PARAM_VALUE events.
    //
    // Since we're accessing the opposite thread's arrays, we must acquire the
    // sync params mutex. And although we need to check the
    // `main_params_changed` array, we mustn't actually modify the
    // `audio_params` array since that can only be done on the audio thread.
    //
    // Don't worry -- it'll pick up the changes eventually.
    //
    std::lock_guard lock(sync_params);

    return main_params_changed[i] ? main_params[i] : audio_params[i];
}

bool MyPlugin::ParamValueToText(const clap_id id, const double value,
                                char* display, const uint32_t size)
{
    uint32_t i = id;
    if (i >= MyPlugin::NumParams) {
        return false;
    }

    snprintf(display, size, "%f", value);

    return true;
}

std::optional<double> MyPlugin::ParamTextToValue(const clap_id id, const char* display)
{
    return false;
}

bool MyPlugin::LoadState(const clap_istream_t* stream)
{
    // Since we're modifying a parameter array, we need to acquire the
    // sync_params mutex.
    std::lock_guard lock(sync_params);

    const auto bytes_to_read = sizeof(float) * NumParams;
    const auto bytes_read    = stream->read(stream, main_params, bytes_to_read);

    const bool success = (bytes_read == bytes_to_read);

    // Make sure that the audio thread will pick up upon the modified
    // parameters next time pluginClass.process is called.
    for (uint32_t i = 0; i < NumParams; ++i) {
        main_params_changed[i] = true;
    }

    return success;
}

bool MyPlugin::SaveState(const clap_ostream_t* stream)
{
    // Synchronize any changes from the audio thread (that is, parameter
    // values sent to us by the host) before we save the state of the
    // plugin.
    SyncAudioParamsToMain();

    const auto bytes_to_write = sizeof(float) * NumParams;
    const auto bytes_written = stream->write(stream, main_params, bytes_to_write);

    return (bytes_written == bytes_to_write);
}

void MyPlugin::Flush(const clap_input_events_t* in, const clap_output_events_t* out)
{
    const uint32_t num_events = in->size(in);

    // For parameters that have been modified by the main thread,
    // send CLAP_EVENT_PARAM_VALUE events to the host.
    SyncMainParamsToAudio(out);

    // Process events sent to our plugin from the host.
    for (uint32_t event_index = 0; event_index < num_events; ++event_index) {
        ProcessEvent(in->get(in, event_index));
    }
}

void MyPlugin::ProcessEvent(const clap_event_header_t* event)
{
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {

        switch (event->type) {
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            const auto note_event = reinterpret_cast<const clap_event_note_t*>(event);

            // Look through our voices array, and if the event
            // matches any of them, it must have been released.
            for (size_t i = 0; i < voices.size(); ++i) {
                auto voice = &voices[i];

                if ((note_event->key == -1 || voice->key == note_event->key) &&
                    (note_event->note_id == -1 || voice->note_id == note_event->note_id) &&
                    (note_event->channel == -1 ||
                     voice->channel == note_event->channel)) {

                    if (event->type == CLAP_EVENT_NOTE_CHOKE) {
                        // Stop the voice immediately; don't process the
                        // release segment of any ADSR envelopes.
                        voices.erase(voices.cbegin() + i);
                        --i;
                    } else {
                        voice->held = false;
                    }
                }
            }

            // If this is a note on event, create a new voice
            // and add it to our vector.
            if (event->type == CLAP_EVENT_NOTE_ON) {
                Voice voice = {.held    = true,
                               .note_id = note_event->note_id,
                               .channel = note_event->channel,
                               .key     = note_event->key,
                               .phase   = 0.0f};

                voices.emplace_back(voice);
            }
        } break;

        case CLAP_EVENT_NOTE_EXPRESSION: {
            [[maybe_unused]] const auto note_expression_event =
                reinterpret_cast<const clap_event_note_expression_t*>(event);
            // TODO
        } break;

        case CLAP_EVENT_PARAM_VALUE: {
            const auto value_event =
                reinterpret_cast<const clap_event_param_value_t*>(event);

            auto i = value_event->param_id;

            std::lock_guard lock(sync_params);

            audio_params[i]         = value_event->value;
            audio_params_changed[i] = true;
        } break;

        case CLAP_EVENT_PARAM_MOD: {
            const auto mod_event = reinterpret_cast<const clap_event_param_mod_t*>(event);

            for (size_t i = 0; i < voices.size(); ++i) {
                auto voice = &voices[i];

                if ((mod_event->key == -1 || voice->key == mod_event->key) &&
                    (mod_event->note_id == -1 || voice->note_id == mod_event->note_id) &&
                    (mod_event->channel == -1 ||
                     voice->channel == mod_event->channel)) {

                    voice->param_offsets[mod_event->param_id] = mod_event->amount;
                    break;
                }
            }
        } break;

        case CLAP_EVENT_TRANSPORT: {
            [[maybe_unused]] const auto transport_event =
                reinterpret_cast<const clap_event_transport_t*>(event);
            // TODO
        } break;

        case CLAP_EVENT_MIDI: {
            [[maybe_unused]] const auto midi_event = reinterpret_cast<const clap_event_midi_t*>(event);
            // TODO
        } break;

        case CLAP_EVENT_MIDI_SYSEX: {
            [[maybe_unused]] const auto sysex_event = reinterpret_cast<const clap_event_midi_sysex*>(event);
            // TODO
        } break;

        case CLAP_EVENT_MIDI2: {
            [[maybe_unused]] const auto midi2_event = reinterpret_cast<const clap_event_midi2*>(event);
            // TODO
        } break;
        }
    }
}

static float triangle(const float x)
{
    // amplitude
    constexpr auto A = 2.0f;

    // period
    constexpr auto P = 1.0f * M_PI;

    constexpr auto AmplitudeOffset = A / 2.0f;
    constexpr auto PhaseOffset     = P / -2.0f;

    return (A / P) * (P - fabs(fmod(x - PhaseOffset, 2.0f * P) - P)) - AmplitudeOffset;
}

void MyPlugin::RenderAudio(const uint32_t num_frames)
{
    for (size_t frame = 0; frame < num_frames; ++frame) {
        auto sum = 0.0f;

        for (size_t i = 0; i < voices.size(); ++i) {
            auto voice = &voices[i];
            if (!voice->held) {
                continue;
            }

            const auto volume = std::clamp(audio_params[ParamVolume] +
                                               voice->param_offsets[ParamVolume],
                                           0.0f,
                                           1.0f);

            switch (waveform) {
            case Waveform::Sine:
                sum += sinf(voice->phase * 2.0f * M_PI) * 0.2f * volume;
                break;

            case Waveform::Triangle:
                sum += triangle(voice->phase * 2.0f * M_PI) * 0.2f * volume;
                break;

            default: assert(false);
            }

            voice->phase += 440.0f * exp2f((voice->key - 57.0f) / 12.0f) / RenderSampleRateHz;
            voice->phase -= floorf(voice->phase);
        }

        render_buf[0].emplace_back(sum);
        render_buf[1].emplace_back(sum);
    }
}

void MyPlugin::SyncMainParamsToAudio(const clap_output_events_t* out)
{
    std::lock_guard lock(sync_params);

    for (uint32_t i = 0; i < NumParams; ++i) {
        if (main_params_changed[i]) {
            audio_params[i]        = main_params[i];
            main_params_changed[i] = false;

            clap_event_param_value_t event = {
                .header = {
                    .size     = sizeof(event),
                    .time     = 0,
                    .space_id = CLAP_CORE_EVENT_SPACE_ID,
                    .type     = CLAP_EVENT_PARAM_VALUE,
                    .flags    = 0
                },
                .param_id   = i,
                .cookie     = nullptr,
                .note_id    = -1,
                .port_index = -1,
                .channel    = -1,
                .key        = -1,
                .value      = audio_params[i]
            };

            out->try_push(out, &event.header);
        }
    }
}

bool MyPlugin::SyncAudioParamsToMain()
{
    bool any_changed = false;

    std::lock_guard lock(sync_params);

    for (uint32_t i = 0; i < NumParams; ++i) {
        if (audio_params_changed[i]) {
            main_params[i]          = audio_params[i];
            audio_params_changed[i] = false;

            any_changed = true;
        }
    }

    return any_changed;
}
