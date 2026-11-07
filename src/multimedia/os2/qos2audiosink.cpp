// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2audiosink_p.h"
#include "qos2audiodevice_p.h"
#include "qos2audiostream_p.h"

#include <QtCore/qloggingcategory.h>
#include <QtMultimedia/private/qaudiohelpers_p.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(qLcOS2AudioSink, "qt.multimedia.os2.audiosink")

namespace {
constexpr int TimerIntervalMs = 20;
} // namespace

QOS2AudioSink::QOS2AudioSink(QAudioDevice device, const QAudioFormat &format, QObject *parent)
    : QPlatformAudioSink(std::move(device), format, parent)
{
    m_hostFormat = m_format;
    m_hostFormat.setSampleFormat(QOS2AudioDeviceInfo::hostSampleFormat(m_format.sampleFormat()));

    m_stream = QOS2AudioStream::create(QOS2AudioStream::Playback, &m_ring);
    m_timer = new QTimer(this);
    m_timer->setInterval(TimerIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &QOS2AudioSink::onTimer);
}

QOS2AudioSink::~QOS2AudioSink()
{
    close();
    delete m_stream;
}

qsizetype QOS2AudioSink::hostToAppBytes(qsizetype hostBytes) const
{
    if (m_hostFormat.bytesPerFrame() <= 0)
        return 0;
    return m_format.bytesForFrames(m_hostFormat.framesForBytes(hostBytes));
}

qsizetype QOS2AudioSink::appToHostBytes(qsizetype appBytes) const
{
    if (m_format.bytesPerFrame() <= 0)
        return 0;
    return m_hostFormat.bytesForFrames(m_format.framesForBytes(appBytes));
}

bool QOS2AudioSink::open()
{
    if (!QOS2AudioDeviceInfo::isSupportedFormat(m_format)) {
        qCWarning(qLcOS2AudioSink) << "Unsupported audio format" << m_format;
        setError(QAudio::OpenError);
        return false;
    }

    if (!m_stream->open(QOS2AudioDeviceInfo::deviceIndex(m_audioDevice.id()), m_hostFormat)) {
        setError(QAudio::OpenError);
        return false;
    }

    const int bpf = m_hostFormat.bytesPerFrame();
    const qsizetype requested = appToHostBytes(m_requestedBufferSize);
    qsizetype ringSize = requested;
    const qsizetype minimum = 2 * m_stream->bufferBytes();
    if (ringSize < minimum)
        ringSize = requested > 0 ? minimum : m_stream->bufferBytes() * m_stream->bufferCount();
    ringSize -= ringSize % bpf;
    m_ring.resize(ringSize);
    m_partialFrame.clear();
    m_lastXruns = 0;

    // Prime the ring buffer so playback doesn't start with silence
    if (m_pullMode)
        fillFromSource();

    if (!m_stream->start()) {
        m_stream->close();
        setError(QAudio::OpenError);
        return false;
    }

    m_lastXruns = m_stream->xrunCount();
    m_timer->start();
    return true;
}

void QOS2AudioSink::close()
{
    m_timer->stop();
    m_stream->close();
    m_ring.clear();
    m_partialFrame.clear();
    if (!m_pullMode)
        delete m_source;
    m_source = nullptr;
}

void QOS2AudioSink::start(QIODevice *device)
{
    close();
    m_pullMode = true;
    m_source = device;
    setError(QAudio::NoError);
    elapsedTime.restart();

    if (!open()) {
        m_source = nullptr;
        setState(QAudio::StoppedState);
        return;
    }
    setState(QAudio::ActiveState);
}

QIODevice *QOS2AudioSink::start()
{
    close();
    m_pullMode = false;
    m_source = new QOS2AudioSinkDevice(this);
    m_source->open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    setError(QAudio::NoError);
    elapsedTime.restart();

    if (!open()) {
        // Keep the device alive so the caller gets a valid (but inert) object
        setState(QAudio::StoppedState);
        return m_source;
    }
    setState(QAudio::IdleState);
    return m_source;
}

void QOS2AudioSink::stop()
{
    if (m_state == QAudio::StoppedState)
        return;
    close();
    setError(QAudio::NoError);
    setState(QAudio::StoppedState);
}

void QOS2AudioSink::reset()
{
    m_ring.clear();
    stop();
}

void QOS2AudioSink::suspend()
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return;
    m_stream->pause();
    m_timer->stop();
    setError(QAudio::NoError);
    setState(QAudio::SuspendedState);
}

void QOS2AudioSink::resume()
{
    if (m_state != QAudio::SuspendedState)
        return;
    m_stream->resume();
    m_lastXruns = m_stream->xrunCount();
    m_timer->start();
    setError(QAudio::NoError);
    setState(m_pullMode ? QAudio::ActiveState : QAudio::IdleState);
}

qsizetype QOS2AudioSink::bytesFree() const
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return 0;
    return hostToAppBytes(m_ring.free());
}

void QOS2AudioSink::setBufferSize(qsizetype value)
{
    if (m_state == QAudio::StoppedState)
        m_requestedBufferSize = value;
}

qsizetype QOS2AudioSink::bufferSize() const
{
    if (m_stream->isOpen())
        return hostToAppBytes(m_ring.capacity());
    return m_requestedBufferSize;
}

qint64 QOS2AudioSink::processedUSecs() const
{
    const int bpf = m_hostFormat.bytesPerFrame();
    if (bpf <= 0 || m_hostFormat.sampleRate() <= 0)
        return 0;
    qint64 bytes = m_stream->processedBytes();
    // Data handed to the driver is still queued in its other buffers for a
    // while; report what has actually reached the speakers so that A/V sync
    // based on this value is accurate.
    if (m_stream->isOpen())
        bytes = qMax<qint64>(0, bytes - qint64(qMax(0, m_stream->bufferCount() - 1))
                                                * m_stream->bufferBytes());
    const qint64 frames = bytes / bpf;
    return frames * 1000000 / m_hostFormat.sampleRate();
}

QAudio::State QOS2AudioSink::state() const
{
    return m_state;
}

// Writes whole frames of application data into the ring buffer, converting
// to the host sample format and applying the volume. Returns the number of
// application bytes consumed.
qint64 QOS2AudioSink::writeToRing(const char *data, qint64 len)
{
    const qint64 frames = qMin<qint64>(m_format.framesForBytes(len),
                                       m_hostFormat.framesForBytes(m_ring.free()));
    if (frames <= 0)
        return 0;

    const qint64 appBytes = m_format.bytesForFrames(frames);
    const qint64 hostBytes = m_hostFormat.bytesForFrames(frames);
    const float vol = volume();
    const bool convert = m_format.sampleFormat() != m_hostFormat.sampleFormat();

    if (!convert && vol >= 1.0f)
        return m_ring.write(data, hostBytes);

    if (m_scratch.size() < hostBytes)
        m_scratch.resize(hostBytes);

    if (convert) {
        using namespace QAudioHelperInternal;
        convertSampleFormat(
                QSpan<const std::byte>(reinterpret_cast<const std::byte *>(data), appBytes),
                toNativeSampleFormat(m_format.sampleFormat()),
                QSpan<std::byte>(reinterpret_cast<std::byte *>(m_scratch.data()), hostBytes),
                toNativeSampleFormat(m_hostFormat.sampleFormat()));
        if (vol < 1.0f)
            QAudioHelperInternal::qMultiplySamples(vol, m_hostFormat, m_scratch.constData(),
                                                   m_scratch.data(), int(hostBytes));
    } else {
        QAudioHelperInternal::qMultiplySamples(vol, m_hostFormat, data, m_scratch.data(),
                                               int(hostBytes));
    }

    m_ring.write(m_scratch.constData(), hostBytes);
    return appBytes;
}

qint64 QOS2AudioSink::pushData(const char *data, qint64 len)
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return 0;

    const qint64 written = writeToRing(data, len);
    if (written > 0 && m_state == QAudio::IdleState) {
        m_lastXruns = m_stream->xrunCount();
        setError(QAudio::NoError);
        setState(QAudio::ActiveState);
    }
    return written;
}

void QOS2AudioSink::fillFromSource()
{
    if (!m_source)
        return;

    const int bpf = qMax(1, m_format.bytesPerFrame());

    for (;;) {
        qsizetype free = hostToAppBytes(m_ring.free());
        if (free <= 0)
            break;

        // Complete a frame left over from a previous short read first
        const qsizetype carried = m_partialFrame.size();
        free -= carried;
        if (free <= 0)
            break;
        m_readBuffer.resize(carried + free);
        if (carried)
            memcpy(m_readBuffer.data(), m_partialFrame.constData(), carried);

        const qint64 got = m_source->read(m_readBuffer.data() + carried, free);
        if (got < 0) {
            qCWarning(qLcOS2AudioSink) << "Error reading from source device";
            close();
            setError(QAudio::IOError);
            setState(QAudio::StoppedState);
            return;
        }
        if (got == 0)
            break;

        const qint64 total = carried + got;
        const qint64 whole = total - total % bpf;
        const qint64 written = whole > 0 ? writeToRing(m_readBuffer.constData(), whole) : 0;
        m_partialFrame = QByteArray(m_readBuffer.constData() + written, total - written);

        if (written > 0 && m_state == QAudio::IdleState) {
            m_lastXruns = m_stream->xrunCount();
            setError(QAudio::NoError);
            setState(QAudio::ActiveState);
        }
        if (got < free)
            break;
    }
}

void QOS2AudioSink::checkUnderrun()
{
    const int xruns = m_stream->xrunCount();
    if (xruns == m_lastXruns)
        return;
    m_lastXruns = xruns;

    if (m_state == QAudio::ActiveState && m_ring.used() == 0) {
        setError(QAudio::UnderrunError);
        setState(QAudio::IdleState);
    }
}

void QOS2AudioSink::onTimer()
{
    if (m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
        return;

    if (m_pullMode)
        fillFromSource();

    if (m_state == QAudio::ActiveState || m_state == QAudio::IdleState)
        checkUnderrun();
}

void QOS2AudioSink::setState(QAudio::State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

QT_END_NAMESPACE

#include "moc_qos2audiosink_p.cpp"
