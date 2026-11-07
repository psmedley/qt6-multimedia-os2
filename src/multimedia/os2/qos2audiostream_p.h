// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QOS2AUDIOSTREAM_P_H
#define QOS2AUDIOSTREAM_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//
#include <QtCore/qatomic.h>
#include <QtCore/qlist.h>
#include <QtCore/qmutex.h>
#include <QtCore/qpair.h>
#include <QtCore/qstring.h>
#include <QtMultimedia/qaudioformat.h>

QT_BEGIN_NAMESPACE

class QOS2AudioRingBuffer;

// Common interface for the OS/2 audio stream implementations (KAI and DART).
//
// For playback the driver callback pulls data out of the ring buffer
// (padding with silence on underrun). For recording the callback pushes
// captured data into the ring buffer.
class QOS2AudioStream
{
public:
    enum Direction { Playback, Record };
    enum Backend { Dart, Kai };

    virtual ~QOS2AudioStream();

    // deviceIndex 0 is the system default device, 1..N select a specific one.
    virtual bool open(unsigned short deviceIndex, const QAudioFormat &format) = 0;
    virtual bool start() = 0;
    virtual void close() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool isOpen() const = 0;

    qsizetype bufferBytes() const { return m_bufferBytes; }
    int bufferCount() const { return m_bufferCount; }
    qint64 processedBytes() const;
    void resetProcessedBytes();
    int xrunCount() const { return m_xruns.loadRelaxed(); }

    // Backend selection. initializeBackend() picks KAI when it was built in
    // and initializes successfully (unless QT_OS2_AUDIO_BACKEND=dart), and
    // falls back to DART otherwise.
    static void initializeBackend();
    static void shutdownBackend();
    static Backend backend(Direction direction);

    static QOS2AudioStream *create(Direction direction, QOS2AudioRingBuffer *ring);
    static QList<QPair<unsigned short, QString>> availableDevices(Direction direction);

protected:
    QOS2AudioStream(Direction direction, QOS2AudioRingBuffer *ring);

    static bool silenceFor(const QAudioFormat &format, int *bitsPerSample, char *silence);
    static qsizetype preferredBufferBytes(const QAudioFormat &format);

    // Called from the driver callback thread
    void fillPlayback(char *data, qsizetype len);
    void storeCaptured(const char *data, qsizetype len);

    Direction m_direction;
    QOS2AudioRingBuffer *m_ring;
    qsizetype m_bufferBytes = 0;
    int m_bufferCount = 0;
    char m_silence = 0;

    QAtomicInt m_stopping = 0;
    QAtomicInt m_xruns = 0;
    QMutex m_callbackMutex;

private:
    mutable QMutex m_counterMutex;
    qint64 m_processedBytes = 0;
};

QT_END_NAMESPACE

#endif // QOS2AUDIOSTREAM_P_H
