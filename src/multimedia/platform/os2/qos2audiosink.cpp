/****************************************************************************
**
** Copyright (C) 2026 Paul Smedley
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/


#include "qos2audiosink_p.h"
#include "qos2audiodevice_p.h"
#include "qos2audiostream_p.h"

#include <QtCore/qloggingcategory.h>
#include <QtMultimedia/private/qaudiohelpers_p.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcOS2AudioSink, "qt.multimedia.os2.audiosink")

namespace {
constexpr int TimerIntervalMs = 20;

bool isSupportedFormat(const QAudioFormat &format)
{
    return format.isValid()
        && (format.sampleFormat() == QAudioFormat::UInt8
            || format.sampleFormat() == QAudioFormat::Int16)
        && format.channelCount() >= 1 && format.channelCount() <= 2;
}
} // namespace

QOS2AudioSink::QOS2AudioSink(const QByteArray &device, QObject *parent)
    : QPlatformAudioSink(parent),
      m_device(device)
{
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

bool QOS2AudioSink::open()
{
    if (!isSupportedFormat(m_format)) {
        qCWarning(qLcOS2AudioSink) << "Unsupported audio format" << m_format;
        setError(QAudio::OpenError);
        return false;
    }

    if (!m_stream->open(QOS2AudioDeviceInfo::deviceIndex(m_device), m_format)) {
        setError(QAudio::OpenError);
        return false;
    }

    const int bpf = m_format.bytesPerFrame();
    qsizetype ringSize = m_requestedBufferSize;
    const qsizetype minimum = 2 * m_stream->bufferBytes();
    if (ringSize < minimum)
        ringSize = m_requestedBufferSize > 0 ? minimum
                                             : m_stream->bufferBytes() * m_stream->bufferCount();
    ringSize -= ringSize % bpf;
    m_ring.resize(ringSize);
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
    if (!m_pullMode)
        delete m_source;
    m_source = nullptr;
}

void QOS2AudioSink::start(QIODevice *device)
{
    close();

    m_pullMode = true;
    m_source = device;
    m_error = QAudio::NoError;

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
    m_error = QAudio::NoError;

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
    const int bpf = m_format.bytesPerFrame();
    const qsizetype free = m_ring.free();
    return bpf > 0 ? free - free % bpf : free;
}

void QOS2AudioSink::setBufferSize(qsizetype value)
{
    if (m_state == QAudio::StoppedState)
        m_requestedBufferSize = value;
}

qsizetype QOS2AudioSink::bufferSize() const
{
    if (m_stream->isOpen())
        return m_ring.capacity();
    return m_requestedBufferSize;
}

qint64 QOS2AudioSink::processedUSecs() const
{
    const int bpf = m_format.bytesPerFrame();
    if (bpf <= 0 || m_format.sampleRate() <= 0)
        return 0;
    const qint64 frames = m_stream->processedBytes() / bpf;
    return frames * 1000000 / m_format.sampleRate();
}

QAudio::Error QOS2AudioSink::error() const
{
    return m_error;
}

QAudio::State QOS2AudioSink::state() const
{
    return m_state;
}

void QOS2AudioSink::setFormat(const QAudioFormat &format)
{
    if (m_state == QAudio::StoppedState)
        m_format = format;
}

QAudioFormat QOS2AudioSink::format() const
{
    return m_format;
}

void QOS2AudioSink::setVolume(qreal volume)
{
    m_volume = qBound(qreal(0), volume, qreal(1));
}

qreal QOS2AudioSink::volume() const
{
    return m_volume;
}

qint64 QOS2AudioSink::writeToRing(const char *data, qint64 len)
{
    const int bytesPerSample = m_format.bytesPerSample();
    qint64 n = qMin<qint64>(len, m_ring.free());
    if (bytesPerSample > 1)
        n -= n % bytesPerSample;
    if (n <= 0)
        return 0;

    if (m_volume < 1.0) {
        if (m_scratch.size() < n)
            m_scratch.resize(n);
        QAudioHelperInternal::qMultiplySamples(m_volume, m_format, data, m_scratch.data(), int(n));
        return m_ring.write(m_scratch.constData(), n);
    }
    return m_ring.write(data, n);
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
    QByteArray buffer;
    for (;;) {
        qsizetype free = m_ring.free();
        free -= free % bpf;
        if (free <= 0)
            break;

        buffer.resize(free);
        const qint64 got = m_source->read(buffer.data(), free);
        if (got < 0) {
            qCWarning(qLcOS2AudioSink) << "Error reading from source device";
            close();
            setError(QAudio::IOError);
            setState(QAudio::StoppedState);
            return;
        }
        if (got == 0)
            break;

        writeToRing(buffer.constData(), got);
        if (m_state == QAudio::IdleState) {
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

void QOS2AudioSink::setError(QAudio::Error error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged(error);
}

QT_END_NAMESPACE

#include "moc_qos2audiosink_p.cpp"
