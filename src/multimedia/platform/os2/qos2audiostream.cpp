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


#include <private/qtmultimediaglobal_p.h>

#include "qos2audiostream_p.h"
#include "qos2audioringbuffer_p.h"
#include "qos2dartstream_p.h"
#if QT_CONFIG(os2kai)
#include "qos2kaistream_p.h"
#endif

#include <QtCore/qbytearray.h>
#include <QtCore/qloggingcategory.h>

#include <string.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcOS2Audio, "qt.multimedia.os2.audio")

namespace {
// Number of buffers queued in the driver and the duration of each.
constexpr int DriverBufferCount = 4;
constexpr int DriverBufferMs = 40;

bool g_kaiActive = false;
} // namespace

QOS2AudioStream::QOS2AudioStream(Direction direction, QOS2AudioRingBuffer *ring)
    : m_direction(direction),
      m_ring(ring),
      m_bufferCount(DriverBufferCount)
{
}

QOS2AudioStream::~QOS2AudioStream() = default;

qint64 QOS2AudioStream::processedBytes() const
{
    QMutexLocker locker(&m_counterMutex);
    return m_processedBytes;
}

void QOS2AudioStream::resetProcessedBytes()
{
    QMutexLocker locker(&m_counterMutex);
    m_processedBytes = 0;
}

bool QOS2AudioStream::silenceFor(const QAudioFormat &format, int *bitsPerSample, char *silence)
{
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        *bitsPerSample = 8;
        *silence = char(0x80);
        return true;
    case QAudioFormat::Int16:
        *bitsPerSample = 16;
        *silence = 0;
        return true;
    default:
        return false;
    }
}

qsizetype QOS2AudioStream::preferredBufferBytes(const QAudioFormat &format)
{
    const int bytesPerFrame = format.bytesPerFrame();
    qsizetype bytes = qsizetype(format.sampleRate()) * DriverBufferMs / 1000 * bytesPerFrame;
    // Keep buffers a multiple of 4 bytes as well as a whole number of frames
    while (bytes % 4)
        bytes += bytesPerFrame;
    return qMax<qsizetype>(bytes, 4 * bytesPerFrame);
}

void QOS2AudioStream::fillPlayback(char *data, qsizetype len)
{
    const qsizetype got = m_ring->read(data, len);
    if (got < len) {
        memset(data + got, m_silence, len - got);
        m_xruns.ref();
    }
    QMutexLocker locker(&m_counterMutex);
    m_processedBytes += got;
}

void QOS2AudioStream::storeCaptured(const char *data, qsizetype len)
{
    const qsizetype written = m_ring->write(data, len);
    if (written < len)
        m_xruns.ref();
    QMutexLocker locker(&m_counterMutex);
    m_processedBytes += len;
}

void QOS2AudioStream::initializeBackend()
{
#if QT_CONFIG(os2kai)
    if (g_kaiActive)
        return;
    const QByteArray requested = qgetenv("QT_OS2_AUDIO_BACKEND").toLower();
    if (requested == "dart") {
        qCDebug(qLcOS2Audio) << "Using DART backend (QT_OS2_AUDIO_BACKEND=dart)";
        return;
    }
    g_kaiActive = QOS2KaiStream::initialize();
    if (!g_kaiActive)
        qCWarning(qLcOS2Audio) << "KAI initialization failed, falling back to DART";
    else
        qCDebug(qLcOS2Audio) << "Using KAI backend";
#endif
}

void QOS2AudioStream::shutdownBackend()
{
#if QT_CONFIG(os2kai)
    if (g_kaiActive)
        QOS2KaiStream::shutdown();
    g_kaiActive = false;
#endif
}

QOS2AudioStream::Backend QOS2AudioStream::backend(Direction direction)
{
#if QT_CONFIG(os2kai)
    if (g_kaiActive && (direction == Playback || QOS2KaiStream::supportsRecording()))
        return Kai;
#else
    Q_UNUSED(direction);
#endif
    return Dart;
}

QOS2AudioStream *QOS2AudioStream::create(Direction direction, QOS2AudioRingBuffer *ring)
{
#if QT_CONFIG(os2kai)
    if (backend(direction) == Kai)
        return new QOS2KaiStream(direction, ring);
#endif
    return new QOS2DartStream(direction, ring);
}

QList<QPair<unsigned short, QString>> QOS2AudioStream::availableDevices(Direction direction)
{
#if QT_CONFIG(os2kai)
    if (backend(direction) == Kai)
        return QOS2KaiStream::availableDevices();
#endif
    return QOS2DartStream::availableDevices();
}

QT_END_NAMESPACE
