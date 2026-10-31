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


#include "qos2audiosource_p.h"
#include "qos2audiodevice_p.h"
#include "qos2dartstream_p.h"

#include <QtCore/qloggingcategory.h>
#include <QtMultimedia/private/qaudiohelpers_p.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcOS2AudioSource, "qt.multimedia.os2.audiosource")

namespace {
constexpr int TimerIntervalMs = 20;
constexpr qint64 DefaultBufferUs = 500000;

bool isSupportedFormat(const QAudioFormat &format)
{
    return format.isValid()
        && (format.sampleFormat() == QAudioFormat::UInt8
            || format.sampleFormat() == QAudioFormat::Int16)
        && format.channelCount() >= 1 && format.channelCount() <= 2;
}
} // namespace

QOS2AudioSource::QOS2AudioSource(const QByteArray &device, QObject *parent)
    : m_device(device)
{
    setParent(parent);
    m_stream = new QOS2DartStream(QOS2DartStream::Record, &m_ring);
    m_timer = new QTimer(this);
    m_timer->setInterval(TimerIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &QOS2AudioSource::onTimer);
}

QOS2AudioSource::~QOS2AudioSource()
{
    close();
    delete m_stream;
}

bool QOS2AudioSource::open()
{
    if (!isSupportedFormat(m_format)) {
        qCWarning(qLcOS2AudioSource) << "Unsupported audio format" << m_format;
        setError(QAudio::OpenError);
        return false;
    }

    // Size the ring buffer before starting; the DART callback writes into it
    // as soon as recording begins.
    if (!m_stream->open(QOS2AudioDeviceInfo::deviceIndex(m_device), m_format)) {
        setError(QAudio::OpenError);
        return false;
    }

    const int bpf = m_format.bytesPerFrame();
    qsizetype ringSize = m_requestedBufferSize;
    const qsizetype minimum = 2 * m_stream->bufferBytes();
    if (ringSize < minimum) {
        ringSize = m_requestedBufferSize > 0
                ? minimum
                : qMax<qsizetype>(minimum, m_format.bytesForDuration(DefaultBufferUs));
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
    m_error = QAudio::NoError;

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
    m_error = QAudio::NoError;

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
    return m_ring.used();
}

void QOS2AudioSource::setBufferSize(qsizetype value)
{
    if (m_state == QAudio::StoppedState)
        m_requestedBufferSize = value;
}

qsizetype QOS2AudioSource::bufferSize() const
{
    if (m_stream->isOpen())
        return m_ring.capacity();
    return m_requestedBufferSize;
}

qint64 QOS2AudioSource::processedUSecs() const
{
    const int bpf = m_format.bytesPerFrame();
    if (bpf <= 0 || m_format.sampleRate() <= 0)
        return 0;
    const qint64 frames = m_stream->processedBytes() / bpf;
    return frames * 1000000 / m_format.sampleRate();
}

QAudio::Error QOS2AudioSource::error() const
{
    return m_error;
}

QAudio::State QOS2AudioSource::state() const
{
    return m_state;
}

void QOS2AudioSource::setFormat(const QAudioFormat &format)
{
    if (m_state == QAudio::StoppedState)
        m_format = format;
}

QAudioFormat QOS2AudioSource::format() const
{
    return m_format;
}

void QOS2AudioSource::setVolume(qreal volume)
{
    m_volume = qBound(qreal(0), volume, qreal(1));
}

qreal QOS2AudioSource::volume() const
{
    return m_volume;
}

qint64 QOS2AudioSource::readCaptured(char *data, qint64 len)
{
    if (m_state == QAudio::StoppedState)
        return 0;

    const int bytesPerSample = m_format.bytesPerSample();
    if (bytesPerSample > 1)
        len -= len % bytesPerSample;
    if (len <= 0)
        return 0;

    const qint64 got = m_ring.read(data, len);
    if (got > 0 && m_volume < 1.0)
        QAudioHelperInternal::qMultiplySamples(m_volume, m_format, data, data, int(got));
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
            m_scratch.resize(qMax<qsizetype>(4096, m_ring.capacity()));
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

void QOS2AudioSource::setError(QAudio::Error error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged(error);
}

QT_END_NAMESPACE

#include "moc_qos2audiosource_p.cpp"
