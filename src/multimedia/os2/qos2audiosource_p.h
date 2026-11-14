// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QOS2AUDIOSOURCE_P_H
#define QOS2AUDIOSOURCE_P_H

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

#include <QtCore/qiodevice.h>
#include <QtCore/qtimer.h>
#include <QtMultimedia/qaudio.h>
#include <QtMultimedia/qaudiodevice.h>
#include <QtMultimedia/qaudioformat.h>
#include <QtMultimedia/private/qaudiosystem_p.h>

#include "qos2audioringbuffer_p.h"

QT_BEGIN_NAMESPACE

class QOS2AudioStream;

class QOS2AudioSource : public QPlatformAudioSource
{
    Q_OBJECT

public:
    QOS2AudioSource(QAudioDevice device, const QAudioFormat &format, QObject *parent);
    ~QOS2AudioSource() override;

    void start(QIODevice *device) override;
    QIODevice *start() override;
    void stop() override;
    void reset() override;
    void suspend() override;
    void resume() override;
    qsizetype bytesReady() const override;
    void setBufferSize(qsizetype value) override;
    qsizetype bufferSize() const override;
    qint64 processedUSecs() const override;
    QAudio::State state() const override;

    // Used by the pull mode QIODevice
    qint64 readCaptured(char *data, qint64 len);

private slots:
    void onTimer();

private:
    bool open();
    void close();
    void setState(QAudio::State state);

    // Conversions between application bytes (m_format) and bytes in the
    // ring buffer / driver (m_hostFormat).
    qsizetype hostToAppBytes(qsizetype hostBytes) const;
    qsizetype appToHostBytes(qsizetype appBytes) const;

    QAudioFormat m_hostFormat;
    QAudio::State m_state = QAudio::StoppedState;
    QIODevice *m_sink = nullptr;
    bool m_pushMode = true; // true: we write into a user supplied QIODevice
    qsizetype m_requestedBufferSize = 0;
    int m_lastXruns = 0;
    QTimer *m_timer = nullptr;
    QByteArray m_scratch;
    QByteArray m_hostScratch;
    QOS2AudioRingBuffer m_ring;
    QOS2AudioStream *m_stream = nullptr;
};

class QOS2AudioSourceDevice : public QIODevice
{
public:
    explicit QOS2AudioSourceDevice(QOS2AudioSource *source) : m_source(source) {}

    void notifyReadyRead() { emit readyRead(); }

protected:
    qint64 readData(char *data, qint64 len) override { return m_source->readCaptured(data, len); }
    qint64 writeData(const char *, qint64) override { return 0; }

private:
    QOS2AudioSource *m_source;
};

QT_END_NAMESPACE

#endif // QOS2AUDIOSOURCE_P_H
