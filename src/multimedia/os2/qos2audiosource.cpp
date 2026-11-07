// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2audiosource_p.h"
#include "qos2audiodevice_p.h"
#include "qos2audiostream_p.h"

#include <QtCore/qloggingcategory.h>
#include <QtMultimedia/private/qaudiohelpers_p.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(qLcOS2AudioSource, "qt.multimedia.os2.audiosource")

namespace {
constexpr int TimerIntervalMs = 20;
constexpr qint64 DefaultBufferUs = 500000;
} // namespace

QOS2AudioSource::QOS2AudioSource(QAudioDevice device, const QAudioFormat &format, QObject *parent)
    : QPlatformAudioSource(std::move(device), format, parent)
{
    m_hostFormat = m_format;
    m_hostFormat.setSampleFormat(QOS2AudioDeviceInfo::hostSampleFormat(m_format.sampleFormat()));

    m_stream = QOS2AudioStream::create(QOS2AudioStream::Record, &m_ring);
    m_timer = new QTimer(this);
    m_timer->setInterval(TimerIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &QOS2AudioSource::onTimer);
}

QOS2AudioSource::~QOS2AudioSource()
{
    close();
    delete m_stream;
}

qsizetype QOS2AudioSource::hostToAppBytes(qsizetype hostBytes) const
{
    if (m_hostFormat.bytesPerFrame() <= 0)
        return 0;
    return m_format.bytesForFrames(m_hostFormat.framesForBytes(hostBytes));
}

qsizetype QOS2AudioSource::appToHostBytes(qsizetype appBytes) const
{
    if (m_format.bytesPerFrame() <= 0)
        return 0;
    return m_hostFormat.bytesForFrames(m_format.framesForBytes(appBytes));
}

bool QOS2AudioSource::open()
{
    if (!QOS2AudioDeviceInfo::isSupportedFormat(m_format)) {
        qCWarning(qLcOS2AudioSource) << "Unsupported audio format" << m_format;
        setError(QAudio::OpenError);
        return false;
    }

    // Size the ring buffer before starting; the driver callback writes into
    // it as soon as recording begins.
    if (!m_stream->open(QOS2AudioDeviceInfo::deviceIndex(m_audioDevice.id()), m_hostFormat)) {
        setError(QAudio::OpenError);
        return false;
    }

    const int bpf = m_hostFormat.bytesPerFrame();
    const qsizetype requested = appToHostBytes(m_requestedBufferSize);
    qsizetype ringSize = requested;
    const qsizetype minimum = 2 * m_stream->bufferBytes();
    if (ringSize < minimum) {
        ringSize = requested > 0
                ? minimum
                : qMax<qsizetype>(minimum, m_hostFormat.bytesForDuration(DefaultBufferUs));
    }
    ringSize -= ringSize % bpf;
    m_ring.resize(ringSize);

    if (!m_stream->start()) {
        m_stream->close();
        setError(QAudio::OpenError);
        return false;
    }

    m_lastXruns = m_stream->xrunCount();
    m_timer->start();
    return true;
}

void QOS2AudioSource::close()
{
    m_timer->stop();
    m_stream->close();
    m_ring.clear();
    if (!m_pushMode)
        delete m_sink;
    m_sink = nullptr;
}

void QOS2AudioSource::start(QIODevice *device)
{
    close();
    m_pushMode = true;
    m_sink = device;
    setError(QAudio::NoError);
    elapsedTime.restart();

    if (!open()) {
        m_sink = nullptr;
        setState(QAudio::StoppedState);
        return;
    }
    setState(QAudio::ActiveState);
}

QIODevice *QOS2AudioSource::start()
{
    close();
    m_pushMode = false;
    m_sink = new QOS2AudioSourceDevice(this);
    m_sink->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    setError(QAudio::NoError);
    elapsedTime.restart();

    if (!open()) {
        setState(QAudio::StoppedState);
        return m_sink;
    }
    setState(QAudio::IdleState);
    return m_sink;
}

void QOS2AudioSource::stop()
{
    if (m_state == QAudio::StoppedState)
        return;
    close();
    setError(QAudio::NoError);
    setState(QAudio::StoppedState);
}

void QOS2AudioSource::reset()
{
    m_ring.clear();
    stop();
}

void QOS2AudioSource::suspend()
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return;
    m_stream->pause();
    m_timer->stop();
    setError(QAudio::NoError);
    setState(QAudio::SuspendedState);
}

void QOS2AudioSource::resume()
{
    if (m_state != QAudio::SuspendedState)
        return;
    m_stream->resume();
    m_lastXruns = m_stream->xrunCount();
    m_timer->start();
    setError(QAudio::NoError);
    setState(QAudio::ActiveState);
}

qsizetype QOS2AudioSource::bytesReady() const
{
    if (m_state == QAudio::StoppedState)
        return 0;
    return hostToAppBytes(m_ring.used());
}

void QOS2AudioSource::setBufferSize(qsizetype value)
{
    if (m_state == QAudio::StoppedState)
        m_requestedBufferSize = value;
}

qsizetype QOS2AudioSource::bufferSize() const
{
    if (m_stream->isOpen())
        return hostToAppBytes(m_ring.capacity());
    return m_requestedBufferSize;
}

qint64 QOS2AudioSource::processedUSecs() const
{
    const int bpf = m_hostFormat.bytesPerFrame();
    if (bpf <= 0 || m_hostFormat.sampleRate() <= 0)
        return 0;
    const qint64 frames = m_stream->processedBytes() / bpf;
    return frames * 1000000 / m_hostFormat.sampleRate();
}

QAudio::State QOS2AudioSource::state() const
{
    return m_state;
}

// Reads whole frames of captured data, converted to the application sample
// format and with the volume applied. Returns the number of bytes stored.
qint64 QOS2AudioSource::readCaptured(char *data, qint64 len)
{
    if (m_state == QAudio::StoppedState)
        return 0;

    const qint64 frames = qMin<qint64>(m_format.framesForBytes(len),
                                       m_hostFormat.framesForBytes(m_ring.used()));
    if (frames <= 0)
        return 0;

    const qint64 hostBytes = m_hostFormat.bytesForFrames(frames);
    const bool convert = m_format.sampleFormat() != m_hostFormat.sampleFormat();

    qint64 got;
    if (convert) {
        if (m_hostScratch.size() < hostBytes)
            m_hostScratch.resize(hostBytes);
        const qint64 hostGot = m_ring.read(m_hostScratch.data(), hostBytes);
        const qint64 gotFrames = m_hostFormat.framesForBytes(hostGot);
        got = m_format.bytesForFrames(gotFrames);

        using namespace QAudioHelperInternal;
        convertSampleFormat(
                QSpan<const std::byte>(reinterpret_cast<const std::byte *>(m_hostScratch.constData()),
                                       m_hostFormat.bytesForFrames(gotFrames)),
                toNativeSampleFormat(m_hostFormat.sampleFormat()),
                QSpan<std::byte>(reinterpret_cast<std::byte *>(data), got),
                toNativeSampleFormat(m_format.sampleFormat()));
    } else {
        got = m_ring.read(data, hostBytes);
    }

    const float vol = volume();
    if (got > 0 && vol < 1.0f)
        QAudioHelperInternal::qMultiplySamples(vol, m_format, data, data, int(got));
    return got;
}

void QOS2AudioSource::onTimer()
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return;

    const int xruns = m_stream->xrunCount();
    if (xruns != m_lastXruns) {
        qCDebug(qLcOS2AudioSource) << "Capture overrun, data was dropped";
        m_lastXruns = xruns;
    }

    if (m_ring.used() <= 0)
        return;

    if (m_pushMode) {
        if (!m_sink)
            return;
        if (m_scratch.isEmpty())
            m_scratch.resize(qMax<qsizetype>(4096, hostToAppBytes(m_ring.capacity())));
        for (;;) {
            const qint64 got = readCaptured(m_scratch.data(), m_scratch.size());
            if (got <= 0)
                break;
            m_sink->write(m_scratch.constData(), got);
        }
    } else {
        if (m_state == QAudio::IdleState)
            setState(QAudio::ActiveState);
        static_cast<QOS2AudioSourceDevice *>(m_sink)->notifyReadyRead();
    }
}

void QOS2AudioSource::setState(QAudio::State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

QT_END_NAMESPACE

#include "moc_qos2audiosource_p.cpp"
