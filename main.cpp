#include <chrono>
#include <iostream>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
#define INBUF_SIZE 4096
const char* hw_type = "cuda";
// Available device types: cuda dxva2 qsv d3d11va opencl vulkan
static AVBufferRef *hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;

class CClock {
private:
    using system_clock_type = std::chrono::time_point<std::chrono::system_clock>;
public:
    CClock() {
        m_start = std::chrono::system_clock::now();
    }
    ~CClock() {
        system_clock_type end = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = end - m_start;
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(diff);
        m_sFrameCount++;
        m_sSumTime += duration.count();
        std::cout << "cost milliseconds times is " << duration.count() << " sum frame is: "<< m_sFrameCount
        << " cost sum time is: " <<  m_sSumTime<< std::endl;
    }

public:
    static size_t m_sFrameCount;
    static size_t m_sSumTime;

private:
    system_clock_type m_start;
};

size_t  CClock::m_sFrameCount = 0;
size_t  CClock::m_sSumTime = 0;

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt) {
            std::cout << "get hw_format " << av_get_pix_fmt_name(*pix_fmts) << std::endl;
            return *p;
        }
    }
    fprintf(stderr, "Failed to get HW surface format. \n");
    return AV_PIX_FMT_NONE;
}

bool hw_init(AVCodecContext *dec_ctx){
    enum AVHWDeviceType type;

    type = av_hwdevice_find_type_by_name(hw_type);
    std::cout << "##################av_hwdevice_find_type_by_name:" << type;
    type = AV_HWDEVICE_TYPE_CUDA;
    if (type == AV_HWDEVICE_TYPE_NONE){
        fprintf(stderr, "Device type %s is not supported.\n", hw_type);
        fprintf(stderr, "Available device types:");
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE){
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        }
        fprintf(stderr, "\n");
        return false;
    }

    for (int i = 0; ; i++){
        const AVCodecHWConfig *config = avcodec_get_hw_config(dec_ctx->codec, i);
        if (!config) {
            fprintf(stderr, "Decode %s does not surport device type %s.\n", dec_ctx->codec->name,
                    av_hwdevice_get_type_name(type));
            return false;
        }

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    dec_ctx->get_format = get_hw_format;

    int err = 0;
    if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0)) < 0){
        fprintf(stderr, "Fail to Create specified HW device.\n");
        return false;
    }
    dec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    return true;
}

void hw_decode(AVCodecContext *dec_ctx, AVPacket *pkt) {
    int ret;
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        std::cout << "Error sending a packet for decoding" << std::endl;
        return;
    }

    AVFrame *frame, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    while (true){
        frame = av_frame_alloc();
        sw_frame = av_frame_alloc();

        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return;
        }else if (ret < 0){
            std::cout << "Error during decoding" << std::endl;
            return;
        }

        if (frame->format == hw_pix_fmt) {
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                std::cout << "Error transferring the data to system memory" << std::endl;
                return;
            }
            else{
                tmp_frame = sw_frame;
            }
        }else{
            tmp_frame = frame;
        }

        av_frame_free(&frame);
        av_frame_free(&tmp_frame);
    }
}

static void soft_decode(AVCodecContext *dec_ctx, AVPacket *pkt) {

    int ret;
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        std::cout << "Error sending a packet for decoding" << std::endl;
    }

    AVFrame *frame = av_frame_alloc();;
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        }else if (ret < 0){
            std::cout << "Error during decoding" << std::endl;
        }
        std::cout << "decode a success frame" << std::endl;
    }

soft_decode_err:
    av_frame_free(&frame);
}

int main() {
    bool enable_hwdecode = false;
    const char *filename = "D:\\project\\soft_ffmpeg_decode\\raw_data.h264";

    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    size_t data_size;

    const AVCodec *codec;
    AVCodecContext *context = NULL;
    AVCodecParserContext *parser = NULL;
    int ret;
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        return 1;
    }

    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cout << "Codec not found\n" << std::endl;
        return 1;
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        std::cout << "Parser not found\n" << std::endl;
        return 1;
    }
    context = avcodec_alloc_context3(codec);
    if (!context) {
        std::cout << "Could not allocate video codec context" << std::endl;
        return 1;
    }

    if (enable_hwdecode) {
        if (!hw_init(context)) {
            std::cout << "hw_init error" << std::endl;
        }
    }

    if (avcodec_open2(context, codec, NULL) < 0) {
        std::cout << "Could not open codec" << std::endl;
        return 1;
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        std::cout << "Could not open file" << std::endl;
        return 1;
    }

    while (!feof(file)) {
        data_size = fread(inbuf, 1, INBUF_SIZE, file);
        if (!data_size) {
            std::cout << "Could read any buf" << std::endl;
            break;
        }

        data = inbuf;
        while (data_size > 0){
            ret = av_parser_parse2(parser, context, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                std::cout << "Error while parsing" << std::endl;
            }
            data += ret;
            data_size -= ret;

            if (pkt->size > 0) {
                std::cout << "Packet size: " << pkt->size << std::endl;
                CClock oClock;
                (void)oClock;

                if (enable_hwdecode){
                    hw_decode(context, pkt);
                }else{
                    soft_decode(context, pkt);
                }

                av_packet_free(&pkt);
                pkt = av_packet_alloc();
            }
        }
    }

    fclose(file);

    av_parser_close(parser);
    avcodec_free_context(&context);
    av_packet_free(&pkt);
    if (hw_device_ctx != NULL) {
        av_buffer_unref(&hw_device_ctx);
    }
    return 0;
}
