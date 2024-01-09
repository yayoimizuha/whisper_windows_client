#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <deque>
#include <cstdint>
#include <nameof.hpp>
#include <fstream>

using namespace std;

#define STR(var) #var

extern "C" {
#include "libavutil/imgutils.h"
#include "libavutil/file.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
}

#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")


struct file_option {
    string FILENAME;
    unsigned int begin;
    unsigned int end;
};

int decode(vector<uint8_t> &audio, map<pair<float, float>, string> &res);

void convert_to_wav16k(const file_option &option, vector<uint8_t> &audio);

void avio_convert_to_wav16k(const vector<char> &input_file, vector<uint8_t> &output_audio);


int main(int argc, char *argv[]) {
    file_option option = {
            R"(C:\Users\tomokazu\Videos\TEDxTokyo - Kathy Matsui - Womenomics - [English] [GOrRAoI37Ls].webm)",
            0,
            0};
//    map<pair<float, float>, string> res;
    cout << option.FILENAME << endl;
    vector<uint8_t> audio;
//    convert_to_wav16k(option, audio);
    ifstream input_file(option.FILENAME, ios::binary | ios::in);
    vector<char> input_data((istreambuf_iterator<char>(input_file)), istreambuf_iterator<char>());
    cout << "file size: " << input_data.size() << endl;
    avio_convert_to_wav16k(input_data, audio);

}

void convert_to_wav16k(const file_option &option, vector<uint8_t> &audio) {
    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, option.FILENAME.c_str(), nullptr, nullptr) != 0) {
        cerr << "avformat_open_input failed" << endl;
        exit(-1);
    }
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        cerr << "avformat_find_stream_info failed" << endl;
        avformat_close_input(&formatContext);
        exit(-1);
    }

    cout << "available streams media type:" << endl;
    for (int i = AVMediaType::AVMEDIA_TYPE_UNKNOWN; i <= AVMediaType::AVMEDIA_TYPE_NB; ++i) {
        cout << "\t" << i << ": " << nameof::nameof_enum(static_cast<AVMediaType>(i)) << endl;
    }

    vector<int> audio_stream_indices = {};
    cout << "found streams media type:" << endl;
    for (int i = 0; i < formatContext->nb_streams; ++i) {
        cout << "  " << i << " :"
             << nameof::nameof_enum(static_cast<AVMediaType>(formatContext->streams[i]->codecpar->codec_type)) << endl;

        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_indices.push_back(i);
        }
    }
    int audio_stream_index = -1;
    switch (audio_stream_indices.size()) {
        case 0:
            cerr << "audio stream not found." << endl;
            avformat_close_input(&formatContext);
            exit(-1);
        case 1:
            audio_stream_index = audio_stream_indices.at(0);
            break;
        default:
            while (audio_stream_index == -1) {
                cout << "select source streams: ";
                for (auto item: audio_stream_indices) {
                    cout << item << " ";
                }
                cout << endl << "index: ";
                int selected_index = -1;
                cin >> selected_index;
                cout << endl;
                if (find(audio_stream_indices.begin(), audio_stream_indices.end(), selected_index) !=
                    audio_stream_indices.end()) {
                    audio_stream_index = selected_index;
                }
            }
    }


    AVStream *audio_stream;
    audio_stream = formatContext->streams[audio_stream_index];

    const AVCodec *audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (audio_codec == nullptr) {
        cerr << "supported " << avcodec_get_name(audio_stream->codecpar->codec_id) << " decoder not found..."
             << endl;
        exit(-1);
    }
    cout << audio_codec->long_name << endl;

    AVCodecContext *codec_context = avcodec_alloc_context3(audio_codec);
    if (codec_context == nullptr) {
        cerr << "avcodec_alloc_context3 failed..." << endl;
    }
}


struct buffer_data {
    char *ptr;
    size_t size; ///< size left in the buffer
    size_t cur; ///< position in the buffer
};

static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    auto *bd = (struct buffer_data *) opaque;
    buf_size = FFMIN(buf_size, bd->size - bd->cur);
//    if (bd->size == 0) {
//        cout << "zero" << endl;
//    }
    printf("ptr:%p size:%zu\n", bd->ptr, bd->size - bd->cur);
    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr + bd->cur, buf_size);
//    bd->ptr += buf_size;
//    bd->size -= buf_size;
    bd->cur += buf_size;
    return buf_size;

}


static int write_packet(void *opaque, uint8_t *buf, int buf_size) {
    auto *bd = (struct buffer_data *) opaque;
    if (bd->size < bd->cur + buf_size) {
        printf("realloc: ptr:%p size:%zu\n", bd->ptr, bd->cur + buf_size);
        bd->ptr = reinterpret_cast<char *>(av_realloc(bd->ptr, bd->cur + buf_size));
        if (bd->ptr == nullptr) {
            exit(ENOMEM);
        }
        bd->size = bd->cur + buf_size;
    }

    memcpy(bd->ptr + bd->cur, buf, buf_size);
    bd->cur += buf_size;
    return buf_size;
}


static int64_t buf_seek(void *opaque, int64_t offset, int whence) {
    auto *bd = (struct buffer_data *) opaque;
    switch (whence) {
        case AVSEEK_SIZE:
            return static_cast<int64_t>(bd->size);
        case SEEK_CUR:
            offset += static_cast<int64_t>(bd->cur);
            break;
        case SEEK_END:
            offset = static_cast<int64_t>(bd->size);
            break;
    }
    bd->cur = offset;
    return offset;
}

void on_frame_decoded(AVFrame *frame, deque<AVFrame *> &frame_buffer) {
    AVFrame *new_ref = av_frame_alloc();
    av_frame_ref(new_ref, frame);
    frame_buffer.push_back(new_ref);
//    printf("Frame decoded PTS: %jd\n", frame->pts);
//    printf("Frame decoded pos: %lld\n", frame->pkt_pos);
}

void avio_convert_to_wav16k(const vector<char> &input_file, vector<uint8_t> &output_audio) {
    AVFormatContext *format_context = nullptr;
    AVIOContext *input_avio_context = nullptr;
    char *input_buffer = nullptr;
    uint8_t *avio_input_context_buffer = nullptr;
    size_t buffer_size, avio_context_buffer_size = 4096;
//    char *input_filename = nullptr;
    int ret = 0;
    struct buffer_data bd = {nullptr};
    vector<int> audio_stream_indices = {};
    int audio_stream_index = -1;
    AVStream *audio_stream = nullptr;
    AVCodec *audio_codec = nullptr;
    AVCodecContext *codec_context = nullptr;
    AVFrame *input_frame;
    AVPacket input_packet = AVPacket();
    deque<AVFrame *> av_frame_buffer;
    AVRational time_base;

    buffer_size = input_file.size();
    input_buffer = static_cast<char *>(av_malloc(buffer_size));
    memcpy(input_buffer, input_file.data(), input_file.size());
    bd.ptr = input_buffer;
    bd.size = buffer_size;
    bd.cur = 0;
    if (!(format_context = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_input_context_buffer = reinterpret_cast<uint8_t *>( av_malloc(avio_context_buffer_size));
    if (!avio_input_context_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    input_avio_context = avio_alloc_context(avio_input_context_buffer, static_cast<int>(avio_context_buffer_size),
                                            0, &bd, &read_packet, nullptr, nullptr);
    if (!input_avio_context) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    format_context->pb = input_avio_context;
    format_context->flags |= AVFMT_FLAG_CUSTOM_IO;
    if ((ret = avformat_open_input(&format_context, nullptr, nullptr, nullptr)) != 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }
    if ((ret = avformat_find_stream_info(format_context, nullptr)) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }

    cout << "available streams media type:" << endl;
    for (int i = AVMediaType::AVMEDIA_TYPE_UNKNOWN; i <= AVMediaType::AVMEDIA_TYPE_NB; ++i) {
        cout << "\t" << i << ": " << nameof::nameof_enum(static_cast<AVMediaType>(i)) << endl;
    }

    cout << "found streams media type:" << endl;
    for (int i = 0; i < format_context->nb_streams; ++i) {
        cout << "  " << i << " :"
             << nameof::nameof_enum(static_cast<AVMediaType>(format_context->streams[i]->codecpar->codec_type)) << endl;

        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_indices.push_back(i);
        }
    }
    switch (audio_stream_indices.size()) {
        case 0:
            cerr << "audio stream not found." << endl;
            avformat_close_input(&format_context);
            exit(-1);
        case 1:
            audio_stream_index = audio_stream_indices.at(0);
            break;
        default:
            while (audio_stream_index == -1) {
                cout << "select source streams: ";
                for (auto item: audio_stream_indices) {
                    cout << to_string(item) + " ";
                }
                cout << endl << "index: ";
                int selected_index = -1;
                cin >> selected_index;
                cout << endl;
                if (find(audio_stream_indices.begin(), audio_stream_indices.end(), selected_index) !=
                    audio_stream_indices.end()) {
                    audio_stream_index = selected_index;
                }
            }
    }

    audio_stream = format_context->streams[audio_stream_index];
    time_base = audio_stream->time_base;

    audio_codec = const_cast<AVCodec *>(avcodec_find_decoder(audio_stream->codecpar->codec_id));
    if (audio_codec == nullptr) {
        ret = AVERROR_DECODER_NOT_FOUND;
        cerr << "supported " << avcodec_get_name(audio_stream->codecpar->codec_id) << " decoder not found..."
             << endl;
        goto end;
    }
    cout << "supported decoder found : "s + audio_codec->long_name << endl;

    codec_context = avcodec_alloc_context3(audio_codec);
    if (codec_context == nullptr) {
        ret = AVERROR(ENOMEM);
        cerr << "avcodec_alloc_context3 failed..." << endl;
        goto end;
    }

    if ((ret = avcodec_parameters_to_context(codec_context, audio_stream->codecpar) < 0)) {
        cerr << "avcodec_parameters_to_context failed" << endl;
        goto end;
    }

    if ((ret = avcodec_open2(codec_context, audio_codec, nullptr) != 0)) {
        cerr << "avcodec_open2 failed" << endl;
        goto end;
    }

    input_frame = av_frame_alloc();
//    input_packet = av_packet_alloc();

    while (bd.size - bd.cur > 0) {
        ret = av_read_frame(format_context, &input_packet);
//        cout << ret << endl;
        if (input_packet.data == nullptr)break;
        if (input_packet.stream_index == audio_stream->index) {
            if (avcodec_send_packet(codec_context, &input_packet) != 0) {
                cerr << "avcodec_send_packet failed" << endl;
                goto end;
            }
            while ((ret = avcodec_receive_frame(codec_context, input_frame) == 0)) {
                on_frame_decoded(input_frame, av_frame_buffer);
            }
        }
        av_packet_unref(&input_packet);
    }
    av_dump_format(format_context, 0, "null (on memory)", 0);

    if (avcodec_send_packet(codec_context, nullptr) != 0) {
        cerr << "avcodec_send_packet failed" << endl;
        goto end;
    }
    while (avcodec_receive_frame(codec_context, input_frame) == 0) {
        on_frame_decoded(input_frame, av_frame_buffer);
    }


    end:
    avcodec_free_context(&codec_context);
    av_free(input_buffer);
    avformat_close_input(&format_context);
//    av_packet_free(input_packet);
    av_frame_free(&input_frame);
    /* note: the internal input_buffer could have changed, and be != avio_input_context_buffer */
    av_freep(&input_avio_context->buffer);
    av_freep(&input_avio_context);
    av_frame_free(&input_frame);
//    av_file_unmap((uint8_t *) input_buffer, buffer_size);
    if (ret < 0) {
        char error_str[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        cerr << "Error occurred: " << error_str << endl;
        exit(AVERROR(ret));
    }
    AVIOContext *output_avio_context = nullptr;
    uint8_t *avio_output_context_buffer = nullptr;
    avio_output_context_buffer = reinterpret_cast<uint8_t *>(av_malloc(avio_context_buffer_size));
    vector<uint8_t> write_out_buf = {};
    char *output_buffer = nullptr;
    struct buffer_data output_bd = {nullptr};
    output_bd.ptr = output_buffer;
    output_bd.size = 0;
    output_avio_context = avio_alloc_context(avio_output_context_buffer, static_cast<int>(avio_context_buffer_size),
                                             AVIO_FLAG_READ_WRITE, &output_bd, &read_packet, &write_packet, &buf_seek);
    if (output_avio_context == nullptr) {
        cerr << "avio_alloc_context failed..." << endl;
    }
    AVFormatContext *output_format_context = nullptr;
    if (avformat_alloc_output_context2(&output_format_context, nullptr, "wav", nullptr) < 0) {
        cerr << "avformat_alloc_output_context2 failed" << endl;
    }
    output_format_context->pb = output_avio_context;
    output_format_context->flags |= AVFMT_FLAG_CUSTOM_IO;

    const AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (output_codec == nullptr) {
        cerr << "wav codec not found" << endl;
        exit(AVERROR(AVERROR_ENCODER_NOT_FOUND));
    } else {
        cout << "output with: "s + output_codec->long_name << endl;
    }
    AVCodecContext *output_codec_context = avcodec_alloc_context3(output_codec);
    if (output_codec_context == nullptr) {
        cerr << "avcodec_alloc_context3 failed..." << endl;
        exit(AVERROR(ENOMEM));
    }
    auto first_buffer = av_frame_buffer.front();
    output_codec_context->ch_layout.nb_channels = 1;
    output_codec_context->sample_rate = 16000;
    output_codec_context->sample_fmt = AV_SAMPLE_FMT_S16;

    output_codec_context->time_base = time_base;

    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
        output_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *output_codec_options = nullptr;

    av_dict_set(&output_codec_options, "ar", "16000", 0);
    av_dict_set(&output_codec_options, "ac", "1", 0);

    if (avcodec_open2(output_codec_context, output_codec_context->codec, &output_codec_options) != 0) {
        cerr << "avcodec_open2 failed ..." << endl;
        exit(-1);
    }

    AVStream *output_stream = avformat_new_stream(output_format_context, output_codec);
    if (output_stream == nullptr) {
        cerr << "avformat_new_stream failed... (line: " << __LINE__ - 2 << ")" << endl;
        exit(-1);
    }

    output_stream->time_base = output_codec_context->time_base;

    if (avcodec_parameters_from_context(output_stream->codecpar, output_codec_context) < 0) {
        cerr << "avcodec_parameters_from_context failed... (line: " << __LINE__ - 1 << ")" << endl;
        exit(-1);
    }

    if (avformat_write_header(output_format_context, nullptr) < 0) {
        cerr << "avformat_write_header failed... (line: " << __LINE__ - 1 << ")" << endl;
        exit(-1);
    }

    while (!av_frame_buffer.empty()) {
        AVFrame *frame = av_frame_buffer.front();
        av_frame_buffer.pop_front();
        int64_t pts = frame->best_effort_timestamp;
        frame->pts = av_rescale_q(pts, time_base, output_codec_context->time_base);
        frame->key_frame = 0;
//        frame->pict_type = AV_PICTURE_TYPE_NONE;
        if (avcodec_send_frame(output_codec_context, frame) != 0) {
            cerr << "avcodec_send_frame failed... (line: " << __LINE__ - 1 << ")" << endl;
            exit(-1);
        }
        av_frame_free(&frame);
        AVPacket output_packet = AVPacket();
        while (avcodec_receive_packet(output_codec_context, &output_packet) == 0) {
            output_packet.stream_index = 0;
            av_packet_rescale_ts(&output_packet, output_codec_context->time_base, output_stream->time_base);
            if (av_interleaved_write_frame(output_format_context, &output_packet) != 0) {
                cerr << "av_interleaved_write_frame failed... (line: " << __LINE__ - 1 << ")" << endl;
                exit(-1);
            }
        }

    }
    if (avcodec_send_frame(output_codec_context, nullptr) != 0) {
        cerr << "avcodec_send_frame failed... (line: " << __LINE__ - 1 << ")" << endl;
        exit(-1);
    }
    AVPacket output_packet = AVPacket();
    while (avcodec_receive_packet(output_codec_context, &output_packet) == 0) {
        output_packet.stream_index = 0;
        av_packet_rescale_ts(&output_packet, output_codec_context->time_base, output_stream->time_base);
        if (av_interleaved_write_frame(output_format_context, &output_packet) != 0) {
            cerr << "av_interleaved_write_frame failed... (line: " << __LINE__ - 1 << ")" << endl;
            exit(-1);
        }
    }


    if (av_write_trailer(output_format_context) != 0) {
        cerr << "av_write_trailer failed... (line: " << __LINE__ - 1 << ")" << endl;
        exit(-1);
    }
    av_dump_format(output_format_context, 0, nullptr, 0);

    ofstream test_output("test.wav", ios::out | ios::binary);
    test_output.write((const char *) &write_out_buf[0], write_out_buf.size() * sizeof(write_out_buf[0]));
    test_output.close();
//    system("pause");

}