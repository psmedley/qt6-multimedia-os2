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


#ifndef QOS2AUDIOSINK_P_H
#define QOS2AUDIOSINK_P_H

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
#include <QtMultimedia/qaudioformat.h>
#include <private/qaudiosystem_p.h>

#include "qos2audioringbuffer_p.h"

QT_BEGIN_NAMESPACE

class QOS2DartStream;

class QOS2AudioSink : public QPlatformAudioSink
{
    Q_OBJECT
public:
    explicit QOS2AudioSink(const QByteArray &device, QObject *parent = nullptr);
    ~QOS2AudioSink() override;

    void start(QIODevice *device) override;
    QIODevice *start() override;
    void stop() override;
    void reset() override;
    void suspend() override;
    void resume() override;
    qsizetype bytesFree() const override;
    void setBufferSize(qsizetype value) override;
    qsizetype bufferSize() const override;
    qint64 processedUSecs() const override;
    QAudio::Error error() const override;
    QAudio::State state() const override;
    void setFormat(const QAudioFormat &format) override;
    QAudioFormat format() const override;
    void setVolume(qreal volume) override;
    qreal volume() const override;

    // Used by the push mode QIODevice
    qint64 pushData(const char *data, qint64 len);

private slots:
    void onTimer();

private:
    bool open();
    void close();
    void fillFromSource();
    void checkUnderrun();
    qint64 writeToRing(const char *data, qint64 len);
    void setState(QAudio::State state);
    void setError(QAudio::Error error);

    QByteArray m_device;
    QAudioFormat m_format;
    QAudio::State m_state = QAudio::StoppedState;
    QAudio::Error m_error = QAudio::NoError;
    QIODevice *m_source = nullptr;
    bool m_pullMode = true;
    qsizetype m_requestedBufferSize = 0;
    qreal m_volume = 1.0;
    int m_lastXruns = 0;
    QTimer *m_timer = nullptr;
    QByteArray m_scratch;
    QOS2AudioRingBuffer m_ring;
    QOS2DartStream *m_stream = nullptr;
};

class QOS2AudioSinkDevice : public QIODevice
{
public:
    explicit QOS2AudioSinkDevice(QOS2AudioSink *sink) : m_sink(sink) {}

protected:
    qint64 readData(char *, qint64) override { return 0; }
    qint64 writeData(const char *data, qint64 len) override { return m_sink->pushData(data, len); }

private:
    QOS2AudioSink *m_sink;
};

QT_END_NAMESPACE

#endif // QOS2AUDIOSINK_P_H
