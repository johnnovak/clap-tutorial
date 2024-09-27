#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <vector>

#include "speex/speex_resampler.h"

// --------------------------------------------------------------------------
// Standalone test case for plugins that use a different internal sample rate
// than the audio host's sample rate.
//
// The tests renders a 440 Hz sine wave at some internal sample rate,
// resamples it to a different output rate using a fixed output chunk size
// (this emulates how audio plugins request samples), and then writes it to a
// WAV file.
//
// This is a very good test for ensuring we're not dropping samples or doing
// anything weird during resampling as even single-sample glitches are very
// audible with sine waves.
// --------------------------------------------------------------------------

constexpr auto NumChannels = 2;

// Test with render rates both lower & higher than the output rate
constexpr double RenderSampleRateHz = 16789.0;
// constexpr double RenderSampleRateHz = 96789.0;
// constexpr double RenderSampleRateHz = 56789.0;

constexpr double OutputSampleRateHz = 48000.0;

constexpr int MaxFrameCount = 1024;

std::array<std::vector<float>, 2> render_buf   = {};
std::array<std::vector<float>, 2> resample_buf = {};

SpeexResamplerState* resampler = nullptr;
double resample_ratio          = 0.0f;

float phase = 0.0f;

typedef struct wavfile_header_s {
    char chunk_id[4];
    int32_t chunk_size;
    char format[4];

    char subchunk_id[4];
    int32_t subchunk1_size;
    int16_t audio_format;
    int16_t num_channels;
    int32_t sample_rate;
    int32_t byte_rate;
    int16_t block_align;
    int16_t bits_per_sample;

    char subchunk2_id[4];
    int32_t subchunk2_size;
} wavfile_header_t;

constexpr auto PcmAudioFormat = 1;

int write_wav_header(FILE* fp, int32_t num_frames)
{
    wavfile_header_t wh = {};

    wh.chunk_id[0] = 'R';
    wh.chunk_id[1] = 'I';
    wh.chunk_id[2] = 'F';
    wh.chunk_id[3] = 'F';

    wh.num_channels    = NumChannels;
    wh.bits_per_sample = 16;

    auto subchunk2_size = num_frames * wh.num_channels * wh.bits_per_sample / 8;

    constexpr auto Subchunk1Size = 16;
    wh.chunk_size = 4 + (8 + Subchunk1Size) + (8 + subchunk2_size);

    wh.format[0] = 'W';
    wh.format[1] = 'A';
    wh.format[2] = 'V';
    wh.format[3] = 'E';

    wh.subchunk_id[0] = 'f';
    wh.subchunk_id[1] = 'm';
    wh.subchunk_id[2] = 't';
    wh.subchunk_id[3] = ' ';

    wh.subchunk1_size = Subchunk1Size;
    wh.audio_format   = PcmAudioFormat;
    wh.sample_rate    = OutputSampleRateHz;

    wh.byte_rate   = wh.sample_rate * wh.num_channels * wh.bits_per_sample / 8;
    wh.block_align = wh.num_channels * wh.bits_per_sample / 8;

    wh.subchunk2_id[0] = 'd';
    wh.subchunk2_id[1] = 'a';
    wh.subchunk2_id[2] = 't';
    wh.subchunk2_id[3] = 'a';
    wh.subchunk2_size  = subchunk2_size;

    size_t write_count = fwrite(&wh, sizeof(wavfile_header_t), 1, fp);

    return (1 != write_count) ? -1 : 0;
}

void render(const int num_frames)
{
    for (auto i = 0; i < num_frames; ++i) {
        auto s = sinf(phase * 2.0f * M_PI) * 0.2f;

        phase += 440.0f / RenderSampleRateHz;
        phase -= floorf(phase);

        render_buf[0].emplace_back(s);
        render_buf[1].emplace_back(s);
    }
}

void write_data(FILE* fp, const float* left, const float* right, const int num_frames)
{
    for (int i = 0; i < num_frames; ++i) {
        int16_t frame[2] = {static_cast<int16_t>(left[i] * INT16_MAX),
                            static_cast<int16_t>(right[i] * INT16_MAX)};

        fwrite(frame, 2 * NumChannels, 1, fp);
    }
}

void init_resampler()
{
    resample_ratio = RenderSampleRateHz / OutputSampleRateHz;

    const spx_uint32_t in_rate_hz = static_cast<spx_uint32_t>(RenderSampleRateHz);
    const spx_uint32_t out_rate_hz = static_cast<spx_uint32_t>(OutputSampleRateHz);

    constexpr auto ResampleQuality = SPEEX_RESAMPLER_QUALITY_DESKTOP;

    resampler = speex_resampler_init(
        NumChannels, in_rate_hz, out_rate_hz, ResampleQuality, nullptr);

    speex_resampler_skip_zeros(resampler);

    const auto max_render_buf_size = static_cast<size_t>(
        static_cast<double>(MaxFrameCount) * resample_ratio * 1.10f);

    render_buf[0].resize(max_render_buf_size);
    render_buf[1].resize(max_render_buf_size);

    render_buf[0].clear();
    render_buf[1].clear();
}

void resample(const int num_resampled_frames)
{
    resample_buf[0].resize(MaxFrameCount);
    resample_buf[1].resize(MaxFrameCount);

    auto input_len        = render_buf[0].size();
    const auto output_len = static_cast<spx_uint32_t>(num_resampled_frames);

    spx_uint32_t in_len  = input_len;
    spx_uint32_t out_len = output_len;

    printf("resample:\n");
    printf("  L IN   in_len: %3d, out_len: %3d\n", in_len, out_len);

    speex_resampler_process_float(
        resampler, 0, render_buf[0].data(), &in_len, resample_buf[0].data(), &out_len);

    printf("  L OUT  in_len: %3d, out_len: %3d\n\n", in_len, out_len);

    in_len  = input_len;
    out_len = output_len;

    //	printf("  R IN   in_len: %4d, out_len: %3d\n", in_len, out_len);

    speex_resampler_process_float(
        resampler, 1, render_buf[1].data(), &in_len, resample_buf[1].data(), &out_len);

    //	printf("  R OUT  in_len: %4d, out_len: %4d\n", in_len, out_len);

    // we're only shrinking here, so no data is lost
    resample_buf[0].resize(out_len);
    resample_buf[1].resize(out_len);

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

        printf("  CASE 2:\n");
        printf("    num_out_frames_remaining: %2d\n", num_out_frames_remaining);

        const auto render_frame_count = static_cast<int>(std::ceil(
            static_cast<double>(num_out_frames_remaining) * resample_ratio));

        printf("    render_frame_count:       %2d\n", render_frame_count);

        render_buf[0].clear();
        render_buf[1].clear();

        resample_buf[0].resize(MaxFrameCount);
        resample_buf[1].resize(MaxFrameCount);

        render(render_frame_count);
        input_len = render_buf[0].size();

        in_len  = render_buf[0].size();
        out_len = num_out_frames_remaining;

        printf("    L IN   in_len: %3d, out_len: %3d\n", in_len, out_len);

        speex_resampler_process_float(resampler,
                                      0,
                                      render_buf[0].data(),
                                      &in_len,
                                      resample_buf[0].data() + curr_out_pos,
                                      &out_len);

        printf("    L OUT  in_len: %3d, out_len: %3d\n", in_len, out_len);

        in_len  = render_buf[1].size();
        out_len = num_out_frames_remaining;

        speex_resampler_process_float(resampler,
                                      1,
                                      render_buf[1].data(),
                                      &in_len,
                                      resample_buf[1].data() + curr_out_pos,
                                      &out_len);

        // we're only shrinking here, so no data is lost
        const auto new_size = curr_out_pos + out_len;
        printf("    resize render_buf from %zu to %u\n", resample_buf[0].size(), new_size);

        resample_buf[0].resize(new_size);
        resample_buf[1].resize(new_size);

        printf("\n");
    }

    if (in_len < input_len) {
        printf("  CASE 1: in_len: %u, input_len: %zu\n", in_len, input_len);

        if (in_len > 0) {
            auto remaining = input_len - in_len;
            printf("    remaining: %zu\n", remaining);
            // Case 1: The input buffer hasn't been fully consumed; we have
            // leftover input samples that we need to keep for the next
            // Process() call.
            //
            render_buf[0].erase(render_buf[0].begin(), render_buf[0].begin() + in_len);

            render_buf[1].erase(render_buf[1].begin(), render_buf[1].begin() + in_len);
        }

        printf("\n");

    } else {
        printf("  CASE 3: clear render_buf\n\n");

        // Case 3: All input samples have been consumed and the output buffer
        // has been completely filled.
        //
        render_buf[0].clear();
        render_buf[1].clear();
    }
}

void sigsegv_handler(int sig)
{
    void* array[20];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 20);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

int main()
{
    // To get backtraces
    signal(SIGSEGV, sigsegv_handler);

    init_resampler();

    FILE* fp = fopen("out.wav", "wb");

    // Write header placeholder
    wavfile_header_t wh = {};
    fwrite(&wh, sizeof(wavfile_header_t), 1, fp);

    // Render sine wave
    constexpr auto SecondsToRender = 3.0;

    const auto num_frames_total = static_cast<int>(OutputSampleRateHz *
                                                   SecondsToRender);

    auto frames_to_write = num_frames_total;

    while (frames_to_write > 0) {
        // const auto chunk_size_frames = 256;
        // Randomise chunk size for extra testing robustness
        const auto chunk_size_frames = rand() % MaxFrameCount;

        printf("------------------------------------\n");
        printf("chunk_size_frames: %d\n", chunk_size_frames);

        auto num_resampled_frames = std::min(frames_to_write, chunk_size_frames);

        const auto num_frames_to_render = std::max(
            static_cast<int>(static_cast<double>(num_resampled_frames) * resample_ratio -
                             render_buf[0].size()),
            0);

        printf("  num_frames_to_render:          %3d\n", num_frames_to_render);
        printf("  render_buf.size (pre-render):  %3zu\n", render_buf[0].size());

        render(num_frames_to_render);

        printf("  render_buf.size (post-render): %3zu\n\n", render_buf[0].size());

        resample(num_resampled_frames);
        write_data(fp, resample_buf[0].data(), resample_buf[1].data(), num_resampled_frames);
        frames_to_write -= num_resampled_frames;
    }

    // Write WAV header
    fseek(fp, 0, SEEK_SET);
    write_wav_header(fp, num_frames_total);

    fclose(fp);

    return 0;
}
