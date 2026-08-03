#include <QCoreApplication>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#include <cstdio>
int main(int argc, char**argv){
    QCoreApplication a(argc,argv);
    auto devs = QMediaDevices::audioOutputs();
    fprintf(stderr, "devices: %d\n", devs.size());
    auto dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) { fprintf(stderr, "no default device\n"); return 1; }
    fprintf(stderr, "dev: %s\n", dev.description().toUtf8().constData());
    QAudioFormat pf = dev.preferredFormat();
    fprintf(stderr, "preferred: %dHz %dch fmt=%d\n", pf.sampleRate(),
            pf.channelCount(), (int)pf.sampleFormat());
    for (int r : {8000,16000,22050,32000,44100,48000,96000})
        for (int c : {1,2}) {
            QAudioFormat f;
            f.setSampleRate(r); f.setChannelCount(c); f.setSampleFormat(QAudioFormat::Int16);
            fprintf(stderr, "  %dHz %dch Int16: %d\n", r, c, dev.isFormatSupported(f));
        }
    fflush(stderr);
    return 0;
}
