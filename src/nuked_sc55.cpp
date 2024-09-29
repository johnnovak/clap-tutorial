#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "nuked_sc55.h"

NukedSc55::NukedSc55(const clap_plugin_t _plugin_class, const clap_host_t* _host)
{
    plugin_class = _plugin_class;
    host         = _host;

    plugin_class.plugin_data = this;
}

const clap_plugin_t* NukedSc55::GetPluginClass()
{
    return &plugin_class;
}

bool NukedSc55::Init(const clap_plugin* _plugin_instance)
{
    plugin_instance = _plugin_instance;
    return true;
}

void NukedSc55::Shutdown()
{
    if (resampler) {
        speex_resampler_destroy(resampler);
        resampler = nullptr;
    }
}

bool NukedSc55::Activate(const double sample_rate, const uint32_t min_frame_count,
                         const uint32_t max_frame_count)
{
    if (output_sample_rate_hz != RenderSampleRateHz) {
        do_resample = true;

        output_sample_rate_hz = sample_rate;

        // Initialise Speex resampler
        resample_ratio = RenderSampleRateHz / output_sample_rate_hz;

        const spx_uint32_t in_rate_hz = static_cast<int>(RenderSampleRateHz);
        const spx_uint32_t out_rate_hz = static_cast<int>(output_sample_rate_hz);

        constexpr auto NumChannels     = 2; // always stereo
        constexpr auto ResampleQuality = SPEEX_RESAMPLER_QUALITY_DESKTOP;

        resampler = speex_resampler_init(
            NumChannels, in_rate_hz, out_rate_hz, ResampleQuality, nullptr);

        speex_resampler_set_rate(resampler, in_rate_hz, out_rate_hz);
        speex_resampler_skip_zeros(resampler);

        const auto max_render_buf_size = static_cast<size_t>(
            static_cast<double>(max_frame_count) * resample_ratio * 1.10f);

        render_buf[0].resize(max_render_buf_size);
        render_buf[1].resize(max_render_buf_size);

    } else {
        do_resample = false;

        output_sample_rate_hz = RenderSampleRateHz;
        resample_ratio        = 1.0;

        render_buf[0].resize(max_frame_count);
        render_buf[1].resize(max_frame_count);
    }

    return true;
}

clap_process_status NukedSc55::Process(const clap_process_t* process)
{
    assert(process->audio_outputs_count == 1);
    assert(process->audio_inputs_count == 0);

    const uint32_t num_frames = process->frames_count;
    const uint32_t num_events = process->in_events->size(process->in_events);

    uint32_t event_index      = 0;
    uint32_t next_event_frame = (num_events == 0) ? num_frames : 0;

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

    auto out_left  = process->audio_outputs[0].data32[0];
    auto out_right = process->audio_outputs[0].data32[1];

    if (do_resample) {
        ResampleAndPublishFrames(num_frames, out_left, out_right);

    } else {
        for (size_t i = 0; i < num_frames; ++i) {
            out_left[i]  = render_buf[0][i];
            out_right[i] = render_buf[1][i];

            render_buf[0].clear();
            render_buf[1].clear();
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

bool NukedSc55::LoadState(const clap_istream_t* stream)
{
    // TODO
    return true;
}

bool NukedSc55::SaveState(const clap_ostream_t* stream)
{
    // TODO
    return 0;
}

void NukedSc55::Flush(const clap_input_events_t* in, const clap_output_events_t* out)
{
    const uint32_t num_events = in->size(in);

    // Process events sent to our plugin from the host.
    for (uint32_t event_index = 0; event_index < num_events; ++event_index) {
        ProcessEvent(in->get(in, event_index));
    }
}

void NukedSc55::ProcessEvent(const clap_event_header_t* event)
{
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {

        switch (event->type) {

        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF: {
            // "Note On" and "Note Off" MIDI events can be sent either as
            // CLAP_EVENT_NOTE_* or raw CLAP_EVENT_MIDI messages.
            //
            // The same event must not be sent twice; it is forbidden for
            // hosts to send the same note event encoded as both
            // CLAP_EVENT_NOTE_* and CLAP_EVENT_MIDI messages.
            //
            // The official advice is that hosts should prefer
            // CLAP_EVENT_NOTE_* messages, so we need to handle both.
            //
            const auto note_event = reinterpret_cast<const clap_event_note_t*>(event);

            // TODO
        } break;

        case CLAP_EVENT_MIDI: {
            [[maybe_unused]] const auto midi_event =
                reinterpret_cast<const clap_event_midi_t*>(event);
            // TODO
        } break;

        case CLAP_EVENT_MIDI_SYSEX: {
            [[maybe_unused]] const auto sysex_event =
                reinterpret_cast<const clap_event_midi_sysex*>(event);
            // TODO
        } break;
        }
    }
}

void NukedSc55::RenderAudio(const uint32_t num_frames)
{
    // TODO
    // render_buf[0].emplace_back(sum);
    // render_buf[1].emplace_back(sum);
}

void NukedSc55::ResampleAndPublishFrames(const uint32_t num_out_frames,
                                         float* out_left, float* out_right)
{
    const auto input_len  = render_buf[0].size();
    const auto output_len = num_out_frames;

    spx_uint32_t in_len  = input_len;
    spx_uint32_t out_len = output_len;

    speex_resampler_process_float(
        resampler, 0, render_buf[0].data(), &in_len, out_left, &out_len);

    in_len  = input_len;
    out_len = output_len;

    speex_resampler_process_float(
        resampler, 1, render_buf[1].data(), &in_len, out_right, &out_len);

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
                                      out_left + curr_out_pos,
                                      &out_len);

        in_len  = render_buf[1].size();
        out_len = num_out_frames_remaining;

        speex_resampler_process_float(resampler,
                                      1,
                                      render_buf[1].data(),
                                      &in_len,
                                      out_right + curr_out_pos,
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
}
