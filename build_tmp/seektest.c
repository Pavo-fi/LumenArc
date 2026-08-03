#include <libavformat/avformat.h>
#include <stdio.h>
#include <string.h>

static void read_first_pts(AVFormatContext *fmt, int vs, int64_t *pts, AVRational *tb)
{
    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs) { *pts = pkt->pts; *tb = fmt->streams[vs]->time_base; break; }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: seektest <file> <target_ms>\n"); return 1; }
    const char *file = argv[1];
    int64_t target_ms = atoll(argv[2]);
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, file, NULL, NULL) < 0) { fprintf(stderr, "open fail\n"); return 1; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { fprintf(stderr, "streaminfo fail\n"); return 1; }
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVStream *st = fmt->streams[vs];
    int64_t start = fmt->start_time == AV_NOPTS_VALUE ? 0 : fmt->start_time;
    printf("start_time=%lld tb=%d/%d\n", (long long)start, st->time_base.num, st->time_base.den);

    // 方式1: stream-index 显式 + AV_TIME_BASE 微秒
    int64_t ts1 = start + target_ms * 1000;
    avformat_seek_file(fmt, vs, INT64_MIN, ts1, ts1, AVSEEK_FLAG_BACKWARD);
    int64_t p1 = AV_NOPTS_VALUE; AVRational t1;
    read_first_pts(fmt, vs, &p1, &t1);
    double r1 = p1 == AV_NOPTS_VALUE ? -1 : av_q2d(av_mul_q((AVRational){p1,1}, t1)) - (start/1000000.0);
    printf("seek1(stream,us):        actual=%.2fs err=%+.2fs\n", r1, r1 - target_ms/1000.0);

    // 方式2: stream-index + stream timebase
    avformat_seek_file(fmt, vs, INT64_MIN, av_rescale_q(target_ms, (AVRational){1,1000}, st->time_base),
                       av_rescale_q(target_ms, (AVRational){1,1000}, st->time_base), AVSEEK_FLAG_BACKWARD);
    int64_t p2 = AV_NOPTS_VALUE; AVRational t2;
    read_first_pts(fmt, vs, &p2, &t2);
    double r2 = p2 == AV_NOPTS_VALUE ? -1 : av_q2d(av_mul_q((AVRational){p2,1}, t2)) - (start/1000000.0);
    printf("seek2(stream,tb):       actual=%.2fs err=%+.2fs\n", r2, r2 - target_ms/1000.0);

    avformat_close_input(&fmt);
    return 0;
}
