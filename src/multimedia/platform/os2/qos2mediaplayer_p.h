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


#ifndef QOS2MEDIAPLAYER_P_H
#define QOS2MEDIAPLAYER_P_H

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
#include <private/qplatformmediaplayer_p.h>

#include <QtCore/qelapsedtimer.h>
#include <QtCore/qiodevice.h>
#include <QtCore/qpointer.h>
#include <QtCore/qtimer.h>
#include <QtCore/qurl.h>
#include <QtMultimedia/qaudioformat.h>
#include <QtMultimedia/qmediametadata.h>

#include <memory>

#include "qos2ffmpegengine_p.h"

QT_BEGIN_NAMESPACE

class QAudioSink;
class QOS2AudioOutput;
class QOS2MediaPlayer;
class QVideoSink;

// Feeds decoded PCM from the engine into a QAudioSink (pull mode) and keeps
// track of the media time of the audio being played.
class QOS2PlayerAudioDevice : public QIODevice
{
public:
    explicit QOS2PlayerAudioDevice(QOS2MediaPlayer *player);

    void resetStream();
    bool isSequential() const override { return true; }
    bool hasPendingData() const { return m_offset < m_chunk.data.size(); }

    // Media time of the first byte handed to the sink since resetStream()
    bool hasBase() const { return m_hasBase; }
    qint64 basePtsUs() const { return m_basePtsUs; }
    // Media time just after the last byte handed to the sink
    qint64 endPtsUs() const { return m_endPtsUs; }

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QOS2MediaPlayer *m_player;
    QOS2FFmpegAudioChunk m_chunk;
    qsizetype m_offset = 0;
    bool m_hasBase = false;
    qint64 m_basePtsUs = 0;
    qint64 m_endPtsUs = 0;
};

class QOS2MediaPlayer : public QObject, public QPlatformMediaPlayer
{
    Q_OBJECT
public:
    explicit QOS2MediaPlayer(QMediaPlayer *player);
    ~QOS2MediaPlayer() override;

    qint64 duration() const override;
    qint64 position() const override;
    void setPosition(qint64 position) override;
    float bufferProgress() const override;
    QMediaTimeRange availablePlaybackRanges() const override;
    qreal playbackRate() const override;
    void setPlaybackRate(qreal rate) override;

    QUrl media() const override;
    const QIODevice *mediaStream() const override;
    void setMedia(const QUrl &media, QIODevice *stream) override;
    bool streamPlaybackSupported() const override { return true; }

    void play() override;
    void pause() override;
    void stop() override;

    void setAudioOutput(QPlatformAudioOutput *output) override;
    void setVideoSink(QVideoSink *sink) override;
    QMediaMetaData metaData() const override;

    int trackCount(TrackType type) override;
    QMediaMetaData trackMetaData(TrackType type, int streamNumber) override;
    int activeTrack(TrackType type) override;
    void setActiveTrack(TrackType type, int streamNumber) override;

    QOS2FFmpegEngine *engine() const { return m_engine.get(); }
    int outputBytesPerSecond() const;

private:
    void onMediaOpened();
    void onOpenFailed(const QString &error);
    void onDecodeError(const QString &error);
    void onTick();

    void resetMedia();
    void configureOutputFormat();
    void applyTracks(qint64 positionUs);
    bool audioEnabled() const;
    bool videoEnabled() const;
    void startAudio();
    void stopAudio();
    void updateAudioVolume();
    void onAudioOutputChanged(bool deviceChanged);
    void startPlayback();
    void handleEndOfMedia();
    qint64 currentPositionUs() const;
    void presentFrame(QOS2FFmpegVideoFrame &frame);
    void notifyPosition(qint64 positionUs, bool force = false);

    QUrl m_url;
    QPointer<QIODevice> m_stream;
    std::unique_ptr<QOS2FFmpegEngine> m_engine;
    QOS2FFmpegMediaInfo m_info;
    QMediaMetaData m_metaData;
    bool m_opened = false;
    bool m_decodingStarted = false;
    bool m_playRequested = false;
    bool m_pauseRequested = false;

    int m_activeAudio = -1;
    int m_activeVideo = -1;

    QOS2AudioOutput *m_audioOutput = nullptr;
    QPointer<QVideoSink> m_videoSink;
    QAudioFormat m_outputFormat;
    QAudioSink *m_audioSink = nullptr;
    QOS2PlayerAudioDevice *m_audioDevice = nullptr;
    bool m_audioFinished = false;
    bool m_audioFailed = false;
    int m_generation = 0;

    // Clock: while audio is playing it is the master, otherwise the system
    // clock is used.
    qint64 m_clockBaseUs = 0;
    QElapsedTimer m_systemClock;
    bool m_systemClockRunning = false;
    qint64 m_frozenPositionUs = 0;
    qint64 m_lastNotifiedMs = -1;
    qint64 m_lastFramePtsUs = -1;
    bool m_needFrame = false;

    QTimer m_tickTimer;
};

QT_END_NAMESPACE

#endif // QOS2MEDIAPLAYER_P_H
