#include <libavformat/avformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now_ms() { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }
int main(int argc, char **argv)
{
    const char *file = argv[1];
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, file, NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVStream *st = fmt->streams[vs];
    int64_t dur_ms = fmt->duration / 1000;
    double t0 = now_ms();
    int64_t prev = -1;
    for (int i = 0; i < 60; ++i) {
        int64_t target_ms = (int64_t)((double)dur_ms * (0.2 + 0.6 * i / 59.0));
        int64_t pts = av_rescale_q(target_ms, (AVRational){1,1000}, st->time_base);
        avformat_seek_file(fmt, vs, INT64_MIN, pts, pts, AVSEEK_FLAG_BACKWARD);
        AVPacket pkt;
        while (av_read_frame(fmt, &pkt) >= 0) {
            if (pkt.stream_index == vs) {
                int64_t rel = av_rescale_q(pkt.pts, st->time_base, (AVRational){1,1000});
                if (prev >= 0 && rel != prev) printf("  i=%d target=%lldms first_pkt=%lldms\n", i, (long long)target_ms, (long long)rel);
                prev = rel;
                av_packet_unref(&pkt);
                break;
            }
            av_packet_unref(&pkt);
        }
    }
    double el = now_ms() - t0;
    printf("60 seeks took %.0fms (%.1fms/seek)\n", el, el/60);
    return 0;
}
