#include "segment_switch_engine.h"

SegmentSwitchEngine::SegmentSwitchEngine(RealFactory factory,
                                         const QVector<SyncSegment> &segs,
                                         QObject *parent)
    : IVideoEngine(parent), m_factory(std::move(factory)), m_segs(segs)
{
}

SegmentSwitchEngine::~SegmentSwitchEngine()
{
    if (m_real) {
        m_real->unload();
        m_real->deleteLater();
    }
}

QVector<qint64> SegmentSwitchEngine::segCum() const
{
    QVector<qint64> cum;
    cum.reserve(m_segs.size());
    qint64 acc = 0;
    for (const auto &s : m_segs) {
        cum.append(acc);
        acc += qMax<qint64>(0, s.durationMs);
    }
    return cum;
}

qint64 SegmentSwitchEngine::totalDuration() const
{
    qint64 acc = 0;
    for (const auto &s : m_segs)
        acc += qMax<qint64>(0, s.durationMs);
    return acc;
}

bool SegmentSwitchEngine::load(const QString & /*filePath*/)
{
    if (m_segs.isEmpty())
        return false;
    if (!m_real) {
        m_real = m_factory(this);
        if (!m_real)
            return false;
        if (m_lowres > 0)
            m_real->setPreviewLowres(m_lowres);
        m_real->setVolume(m_volume);
        m_real->setRate(m_rate);
        connect(m_real, &IVideoEngine::frameReady,
                this, &IVideoEngine::frameReady);
        connect(m_real, &IVideoEngine::stateChanged,
                this, &IVideoEngine::stateChanged);
        connect(m_real, &IVideoEngine::videoSizeChanged,
                this, &IVideoEngine::videoSizeChanged);
        connect(m_real, &IVideoEngine::durationChanged, this,
                [this](qint64 d) {
                    if (m_cur >= 0 && m_cur < m_segs.size()
                        && d > 0 && d != m_segs[m_cur].durationMs) {
                        // 引擎实测为准修正段时长（预读估值→真值，C1 不静默）
                        m_segs[m_cur].durationMs = d;
                        emit durationChanged(totalDuration());
                    }
                    // 换文件落点：seek + 续播意愿
                    if (m_pendingSeek >= 0) {
                        m_real->seek(m_pendingSeek);
                        m_pendingSeek = -1;
                    }
                    if (m_pendingPlay) {
                        m_real->setRate(m_rate);
                        m_real->play();
                        m_pendingPlay = false;
                    }
                });
        connect(m_real, &IVideoEngine::positionChanged, this,
                [this](qint64 realMs) {
                    if (m_cur < 0 || m_cur >= m_segs.size())
                        return;
                    // 段尾自动顺接：下一段墙钟紧邻（≤2s）才换；有缺口交给
                    // 服务覆盖逻辑停路（复出 seek 自然换段）
                    const qint64 segDur = m_segs[m_cur].durationMs;
                    if (m_wantPlay && segDur > 0 && realMs >= segDur - 40
                        && m_cur + 1 < m_segs.size()
                        && m_segs[m_cur + 1].wallStartMs()
                           <= m_segs[m_cur].wallEndMs() + 2000) {
                        // 下一段落点：当前墙钟在下一段校时里的流内时刻
                        const qint64 wall = m_segs[m_cur].cal.wallMsOf(realMs)
                            + m_segs[m_cur].cal.truthOffsetMs;
                        const qint64 nextReal = qBound<qint64>(
                            0, m_segs[m_cur + 1].cal.streamMsOf(
                                   wall - m_segs[m_cur + 1].cal.truthOffsetMs),
                            m_segs[m_cur + 1].durationMs);
                        switchToSegment(m_cur + 1, nextReal, true);
                        return;
                    }
                    // 正常播报：虚拟轴位置
                    const auto cum = segCum();
                    emit positionChanged(cum[m_cur] + realMs);
                });
    }
    m_cur = 0;
    m_pendingSeek = 0;
    m_pendingPlay = false;
    if (!m_real->load(m_segs[0].path))
        return false;
    emit durationChanged(totalDuration());   // 预读总值先行（服务时长落账）
    return true;
}

void SegmentSwitchEngine::switchToSegment(int k, qint64 realMs, bool resumePlay)
{
    if (k < 0 || k >= m_segs.size() || !m_real)
        return;
    m_cur = k;
    m_pendingSeek = qMax<qint64>(0, realMs);
    m_pendingPlay = resumePlay;
    if (m_scrubbing)
        m_real->setScrubMode(true);
    m_real->load(m_segs[k].path);   // 异步；durationChanged 里落点+续播
}

void SegmentSwitchEngine::play()
{
    m_wantPlay = true;
    if (m_real && m_cur >= 0)
        m_real->play();
}

void SegmentSwitchEngine::pause()
{
    m_wantPlay = false;
    if (m_real)
        m_real->pause();
}

void SegmentSwitchEngine::stop()
{
    m_wantPlay = false;
    if (m_real)
        m_real->stop();
}

void SegmentSwitchEngine::unload()
{
    m_wantPlay = false;
    if (m_real)
        m_real->unload();
}

void SegmentSwitchEngine::seek(qint64 virtMs)
{
    if (m_segs.isEmpty() || !m_real)
        return;
    const auto cum = segCum();
    int k = 0;
    for (int i = int(m_segs.size()) - 1; i >= 0; --i)
        if (virtMs >= cum[i]) { k = i; break; }
    const qint64 real = virtMs - cum[k];
    if (k != m_cur) {
        switchToSegment(k, real, m_wantPlay);
    } else {
        m_real->seek(real);
    }
}

qint64 SegmentSwitchEngine::position() const
{
    if (!m_real || m_cur < 0 || m_cur >= m_segs.size())
        return 0;
    return segCum()[m_cur] + m_real->position();
}

qint64 SegmentSwitchEngine::duration() const
{
    return totalDuration();
}

PlaybackState SegmentSwitchEngine::state() const
{
    return m_real ? m_real->state() : PlaybackState::Stopped;
}

int SegmentSwitchEngine::videoWidth() const
{
    return m_real ? m_real->videoWidth() : 0;
}

int SegmentSwitchEngine::videoHeight() const
{
    return m_real ? m_real->videoHeight() : 0;
}

float SegmentSwitchEngine::fps() const
{
    return m_real ? m_real->fps() : 0.0f;
}

void SegmentSwitchEngine::setVolume(int vol)
{
    m_volume = vol;
    if (m_real)
        m_real->setVolume(vol);
}

void SegmentSwitchEngine::setRate(float rate)
{
    m_rate = rate;
    if (m_real)
        m_real->setRate(rate);
}

bool SegmentSwitchEngine::supportsRateAudio() const
{
    return m_real ? m_real->supportsRateAudio() : true;
}

void SegmentSwitchEngine::setScrubMode(bool on)
{
    m_scrubbing = on;
    if (m_real)
        m_real->setScrubMode(on);
}

void SegmentSwitchEngine::setScrubTarget(qint64 virtMs)
{
    // 拖拽追逐目标在虚拟轴：映射在本段 → 直传；跨段 → 换文件落点
    // （跨文件 scrub 追逐是边缘路径，退化为精确 seek，C1 明示注释）
    if (m_segs.isEmpty() || !m_real)
        return;
    const auto cum = segCum();
    int k = 0;
    for (int i = int(m_segs.size()) - 1; i >= 0; --i)
        if (virtMs >= cum[i]) { k = i; break; }
    const qint64 real = virtMs - cum[k];
    if (k != m_cur) {
        switchToSegment(k, real, false);
    } else {
        m_real->setScrubTarget(real);
    }
}

void SegmentSwitchEngine::setPreviewLowres(int level)
{
    m_lowres = level;
    if (m_real)
        m_real->setPreviewLowres(level);
}

void SegmentSwitchEngine::ackFrame()
{
    if (m_real)
        m_real->ackFrame();
}

QString SegmentSwitchEngine::hardwareAdapterName() const
{
    return m_real ? m_real->hardwareAdapterName() : QString();
}

qint64 SegmentSwitchEngine::learnedGopMs() const
{
    return m_real ? m_real->learnedGopMs() : 0;
}
