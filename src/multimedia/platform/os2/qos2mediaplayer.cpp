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


#include "qos2mediaplayer_p.h"
#include "qos2audiooutput_p.h"
#include "qos2ffmpegvideobuffer_p.h"

#include <private/qiso639_2_p.h>

#include <QtCore/qdatetime.h>
#include <QtCore/qloggingcategory.h>
#include <QtGui/qimage.h>
#include <QtMultimedia/qaudiosink.h>
#include <QtMultimedia/qmediadevices.h>
#include <QtMultimedia/qmediatimerange.h>
#include <QtMultimedia/qvideoframe.h>
#include <QtMultimedia/qvideosink.h>

#include <string.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcOS2Player, "qt.multimedia.os2.player")

namespace {
constexpr int TickIntervalMs = 10;
constexpr qint64 PositionNotifyIntervalMs = 100;
constexpr qint64 AudioStarvationUs = 500000;
constexpr qint64 AudioSinkBufferUs = 300000;

void insertText(QMediaMetaData &md, QMediaMetaData::Key key,
                const QMap<QByteArray, QString> &tags, const char *tag)
{
    const QString value = tags.value(tag);
    if (!value.isEmpty() && !md.keys().contains(key))
        md.insert(key, value);
}
} // namespace

// --- QOS2PlayerAudioDevice --------------------------------------------------

QOS2PlayerAudioDevice::QOS2PlayerAudioDevice(QOS2MediaPlayer *player)
    : m_player(player)
{
}

void QOS2PlayerAudioDevice::resetStream()
{
    m_chunk = {};
    m_offset = 0;
    m_hasBase = false;
    m_basePtsUs = 0;
    m_endPtsUs = 0;
}

qint64 QOS2PlayerAudioDevice::readData(char *data, qint64 maxlen)
{
    QOS2FFmpegEngine *engine = m_player->engine();
    const int bytesPerSecond = m_player->outputBytesPerSecond();
    if (!engine || bytesPerSecond <= 0)
        return 0;

    qint64 total = 0;
    while (total < maxlen) {
        if (m_offset >= m_chunk.data.size()) {
            if (!engine->takeAudio(&m_chunk))
                break;
            m_offset = 0;
            if (!m_hasBase) {
                m_hasBase = true;
                m_basePtsUs = m_chunk.ptsUs;
            }
        }
        const qint64 n = qMin<qint64>(maxlen - total, m_chunk.data.size() - m_offset);
        memcpy(data + total, m_chunk.data.constData() + m_offset, size_t(n));
        m_offset += n;
        total += n;
        m_endPtsUs = m_chunk.ptsUs + qint64(m_offset) * 1000000 / bytesPerSecond;
    }
    return total;
}

// --- QOS2MediaPlayer ----------------------------------------------------------

QOS2MediaPlayer::QOS2MediaPlayer(QMediaPlayer *player)
    : QPlatformMediaPlayer(player)
{
    m_audioDevice = new QOS2PlayerAudioDevice(this);
    m_audioDevice->open(QIODevice::ReadOnly);
    m_tickTimer.setInterval(TickIntervalMs);
    m_tickTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_tickTimer, &QTimer::timeout, this, &QOS2MediaPlayer::onTick);
}

QOS2MediaPlayer::~QOS2MediaPlayer()
{
    if (m_audioOutput)
        m_audioOutput->onChanged = nullptr;
    resetMedia();
    delete m_audioDevice;
}

qint64 QOS2MediaPlayer::duration() const
{
    return m_info.durationUs / 1000;
}

qint64 QOS2MediaPlayer::position() const
{
    qint64 ms = currentPositionUs() / 1000;
    if (duration() > 0)
        ms = qMin(ms, duration());
    return qMax<qint64>(0, ms);
}

float QOS2MediaPlayer::bufferProgress() const
{
    return m_opened ? 1.f : 0.f;
}

QMediaTimeRange QOS2MediaPlayer::availablePlaybackRanges() const
{
    if (!m_opened || duration() <= 0)
        return {};
    return QMediaTimeRange(0, duration());
}

qreal QOS2MediaPlayer::playbackRate() const
{
    return 1.;
}

void QOS2MediaPlayer::setPlaybackRate(qreal rate)
{
    if (!qFuzzyCompare(rate, 1.))
        qCWarning(qLcOS2Player) << "Playback rates other than 1.0 are not supported";
}

QUrl QOS2MediaPlayer::media() const
{
    return m_url;
}

const QIODevice *QOS2MediaPlayer::mediaStream() const
{
    return m_stream;
}

int QOS2MediaPlayer::outputBytesPerSecond() const
{
    return m_outputFormat.sampleRate() * m_outputFormat.bytesPerFrame();
}

bool QOS2MediaPlayer::audioEnabled() const
{
    return m_audioOutput && m_activeAudio >= 0;
}

bool QOS2MediaPlayer::videoEnabled() const
{
    return m_videoSink && m_activeVideo >= 0;
}

void QOS2MediaPlayer::resetMedia()
{
    m_tickTimer.stop();
    stopAudio();
    m_engine.reset();
    m_opened = false;
    m_decodingStarted = false;
    m_playRequested = false;
    m_pauseRequested = false;
    m_info = {};
    m_metaData.clear();
    m_activeAudio = -1;
    m_activeVideo = -1;
    m_audioFinished = false;
    m_audioFailed = false;
    m_systemClockRunning = false;
    m_clockBaseUs = 0;
    m_frozenPositionUs = 0;
    m_lastNotifiedMs = -1;
    m_lastFramePtsUs = -1;
    m_needFrame = false;
    if (m_videoSink)
        m_videoSink->setVideoFrame(QVideoFrame());
}

void QOS2MediaPlayer::setMedia(const QUrl &media, QIODevice *stream)
{
    resetMedia();
    stateChanged(QMediaPlayer::StoppedState);

    m_url = media;
    m_stream = stream;

    durationChanged(0);
    positionChanged(0);
    seekableChanged(false);
    audioAvailableChanged(false);
    videoAvailableChanged(false);
    metaDataChanged();
    tracksChanged();

    if (media.isEmpty() && !stream) {
        mediaStatusChanged(QMediaPlayer::NoMedia);
        return;
    }

    mediaStatusChanged(QMediaPlayer::LoadingMedia);
    m_engine = std::make_unique<QOS2FFmpegEngine>();
    // Signals are queued from the decoding thread; ignore any that belong to
    // media that has since been replaced.
    const int generation = ++m_generation;
    connect(m_engine.get(), &QOS2FFmpegEngine::mediaOpened, this, [this, generation]() {
        if (generation == m_generation)
            onMediaOpened();
    });
    connect(m_engine.get(), &QOS2FFmpegEngine::openFailed, this,
            [this, generation](const QString &errorString) {
                if (generation == m_generation)
                    onOpenFailed(errorString);
            });
    connect(m_engine.get(), &QOS2FFmpegEngine::decodeError, this,
            [this, generation](const QString &errorString) {
                if (generation == m_generation)
                    onDecodeError(errorString);
            });
    m_engine->openMedia(media, stream);
}

void QOS2MediaPlayer::onMediaOpened()
{
    if (!m_engine)
        return;

    m_info = m_engine->mediaInfo();
    m_opened = true;
    m_activeAudio = m_info.defaultAudioTrack;
    m_activeVideo = m_info.defaultVideoTrack;

    // Metadata
    m_metaData.clear();
    const auto &tags = m_info.metadata;
    insertText(m_metaData, QMediaMetaData::Title, tags, "title");
    insertText(m_metaData, QMediaMetaData::ContributingArtist, tags, "artist");
    insertText(m_metaData, QMediaMetaData::Author, tags, "artist");
    insertText(m_metaData, QMediaMetaData::Author, tags, "author");
    insertText(m_metaData, QMediaMetaData::AlbumTitle, tags, "album");
    insertText(m_metaData, QMediaMetaData::AlbumArtist, tags, "album_artist");
    insertText(m_metaData, QMediaMetaData::Genre, tags, "genre");
    insertText(m_metaData, QMediaMetaData::Comment, tags, "comment");
    insertText(m_metaData, QMediaMetaData::Description, tags, "description");
    insertText(m_metaData, QMediaMetaData::Copyright, tags, "copyright");
    insertText(m_metaData, QMediaMetaData::Composer, tags, "composer");
    insertText(m_metaData, QMediaMetaData::Publisher, tags, "publisher");
    const QString date = tags.value("date", tags.value("creation_time"));
    if (!date.isEmpty()) {
        QDateTime dt = QDateTime::fromString(date, Qt::ISODate);
        if (dt.isValid())
            m_metaData.insert(QMediaMetaData::Date, dt);
    }
    const QString track = tags.value("track");
    if (!track.isEmpty()) {
        bool ok = false;
        const int number = track.section(QLatin1Char('/'), 0, 0).toInt(&ok);
        if (ok)
            m_metaData.insert(QMediaMetaData::TrackNumber, number);
    }
    if (m_info.durationUs > 0)
        m_metaData.insert(QMediaMetaData::Duration, m_info.durationUs / 1000);
    if (m_activeVideo >= 0) {
        const auto &v = m_info.videoTracks.at(m_activeVideo);
        m_metaData.insert(QMediaMetaData::Resolution, v.size);
        if (v.frameRate > 0)
            m_metaData.insert(QMediaMetaData::VideoFrameRate, v.frameRate);
        if (v.bitRate > 0)
            m_metaData.insert(QMediaMetaData::VideoBitRate, int(v.bitRate));
    }
    if (m_activeAudio >= 0 && m_info.audioTracks.at(m_activeAudio).bitRate > 0)
        m_metaData.insert(QMediaMetaData::AudioBitRate, int(m_info.audioTracks.at(m_activeAudio).bitRate));
    if (!m_info.coverArt.isEmpty()) {
        const QImage cover = QImage::fromData(m_info.coverArt);
        if (!cover.isNull())
            m_metaData.insert(QMediaMetaData::CoverArtImage, cover);
    }

    durationChanged(duration());
    seekableChanged(m_info.seekable);
    audioAvailableChanged(!m_info.audioTracks.isEmpty());
    videoAvailableChanged(!m_info.videoTracks.isEmpty());
    metaDataChanged();
    tracksChanged();
    activeTracksChanged();

    configureOutputFormat();
    m_engine->startDecoding(audioEnabled() ? m_activeAudio : -1,
                            videoEnabled() ? m_activeVideo : -1,
                            m_outputFormat.sampleRate(), m_outputFormat.channelCount(),
                            m_frozenPositionUs);
    m_decodingStarted = true;
    mediaStatusChanged(QMediaPlayer::LoadedMedia);

    // Show the first picture even before playback starts
    m_needFrame = true;
    m_tickTimer.start();

    if (m_playRequested) {
        m_playRequested = false;
        // play() already switched the state; start the clocks now
        startPlayback();
        mediaStatusChanged(QMediaPlayer::BufferedMedia);
    }
    m_pauseRequested = false;
}

void QOS2MediaPlayer::onOpenFailed(const QString &errorString)
{
    m_playRequested = false;
    m_pauseRequested = false;
    stateChanged(QMediaPlayer::StoppedState);
    mediaStatusChanged(QMediaPlayer::InvalidMedia);
    error(QMediaPlayer::ResourceError, errorString);
}

void QOS2MediaPlayer::onDecodeError(const QString &errorString)
{
    qCWarning(qLcOS2Player) << errorString;
}

void QOS2MediaPlayer::configureOutputFormat()
{
    QAudioDevice device = m_audioOutput ? m_audioOutput->device : QAudioDevice();
    if (device.isNull())
        device = QMediaDevices::defaultAudioOutput();

    int rate = 44100;
    int channels = 2;
    if (m_activeAudio >= 0 && m_activeAudio < m_info.audioTracks.size()) {
        rate = m_info.audioTracks.at(m_activeAudio).sampleRate;
        channels = m_info.audioTracks.at(m_activeAudio).channels;
    }
    const int maxChannels = device.maximumChannelCount() > 0 ? device.maximumChannelCount() : 2;
    channels = qBound(1, channels, qMin(2, maxChannels));
    if (device.maximumSampleRate() > 0)
        rate = qBound(device.minimumSampleRate(), rate, device.maximumSampleRate());
    if (rate <= 0)
        rate = 44100;

    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Int16);
    format.setSampleRate(rate);
    format.setChannelCount(channels);
    if (!device.isNull() && !device.isFormatSupported(format)) {
        QAudioFormat preferred = device.preferredFormat();
        preferred.setSampleFormat(QAudioFormat::Int16);
        if (preferred.isValid() && device.isFormatSupported(preferred))
            format = preferred;
    }
    m_outputFormat = format;
}

void QOS2MediaPlayer::applyTracks(qint64 positionUs)
{
    if (!m_decodingStarted || !m_engine)
        return;
    stopAudio();
    configureOutputFormat();
    m_engine->setTracks(audioEnabled() ? m_activeAudio : -1, videoEnabled() ? m_activeVideo : -1,
                        m_outputFormat.sampleRate(), m_outputFormat.channelCount(), positionUs);

    m_frozenPositionUs = positionUs;
    m_clockBaseUs = positionUs;
    m_systemClockRunning = false;
    m_audioFinished = false;
    m_audioFailed = false;
    m_lastFramePtsUs = -1;
    m_needFrame = true;
    if (state() == QMediaPlayer::PlayingState)
        startPlayback();
    m_tickTimer.start();
}

void QOS2MediaPlayer::startAudio()
{
    if (m_audioSink) {
        if (m_audioSink->state() == QAudio::SuspendedState)
            m_audioSink->resume();
        return;
    }

    QAudioDevice device = m_audioOutput ? m_audioOutput->device : QAudioDevice();
    if (device.isNull())
        device = QMediaDevices::defaultAudioOutput();

    m_audioDevice->resetStream();
    m_audioSink = new QAudioSink(device, m_outputFormat, this);
    m_audioSink->setBufferSize(m_outputFormat.bytesForDuration(AudioSinkBufferUs));
    updateAudioVolume();
    m_audioSink->start(m_audioDevice);
    if (m_audioSink->error() != QAudio::NoError || m_audioSink->state() == QAudio::StoppedState) {
        qCWarning(qLcOS2Player) << "Unable to start audio output for" << m_outputFormat
                                << "- continuing without sound";
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioFinished = true;
        m_audioFailed = true;
        m_clockBaseUs = m_frozenPositionUs;
        m_systemClock.start();
        m_systemClockRunning = true;
    }
}

void QOS2MediaPlayer::stopAudio()
{
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    m_audioDevice->resetStream();
}

void QOS2MediaPlayer::updateAudioVolume()
{
    if (m_audioSink && m_audioOutput)
        m_audioSink->setVolume(m_audioOutput->muted ? 0. : qreal(m_audioOutput->volume));
}

void QOS2MediaPlayer::onAudioOutputChanged(bool deviceChanged)
{
    if (deviceChanged && m_decodingStarted && audioEnabled())
        applyTracks(currentPositionUs());
    else
        updateAudioVolume();
}

void QOS2MediaPlayer::setAudioOutput(QPlatformAudioOutput *output)
{
    auto *newOutput = static_cast<QOS2AudioOutput *>(output);
    if (newOutput == m_audioOutput)
        return;
    const bool wasEnabled = audioEnabled();
    if (m_audioOutput)
        m_audioOutput->onChanged = nullptr;
    m_audioOutput = newOutput;
    if (m_audioOutput)
        m_audioOutput->onChanged = [this](bool deviceChanged) { onAudioOutputChanged(deviceChanged); };

    if (m_decodingStarted && (wasEnabled != audioEnabled() || audioEnabled()))
        applyTracks(currentPositionUs());
}

void QOS2MediaPlayer::setVideoSink(QVideoSink *sink)
{
    if (sink == m_videoSink)
        return;
    const bool wasEnabled = videoEnabled();
    m_videoSink = sink;
    if (m_decodingStarted && wasEnabled != videoEnabled())
        applyTracks(currentPositionUs());
    else
        m_needFrame = true;
}

void QOS2MediaPlayer::startPlayback()
{
    if (audioEnabled() && !m_audioFinished) {
        startAudio();
    } else {
        m_clockBaseUs = m_frozenPositionUs;
        m_systemClock.start();
        m_systemClockRunning = true;
    }
    m_tickTimer.start();
}

qint64 QOS2MediaPlayer::currentPositionUs() const
{
    if (state() != QMediaPlayer::PlayingState || !m_opened)
        return m_frozenPositionUs;
    if (m_audioSink && !m_audioFinished) {
        if (!m_audioDevice->hasBase())
            return m_frozenPositionUs;
        return m_audioDevice->basePtsUs() + m_audioSink->processedUSecs();
    }
    if (m_systemClockRunning)
        return m_clockBaseUs + m_systemClock.nsecsElapsed() / 1000;
    return m_frozenPositionUs;
}

void QOS2MediaPlayer::play()
{
    if (mediaStatus() == QMediaPlayer::NoMedia || mediaStatus() == QMediaPlayer::InvalidMedia)
        return;
    if (state() == QMediaPlayer::PlayingState)
        return;

    if (mediaStatus() == QMediaPlayer::EndOfMedia) {
        resetCurrentLoop();
        m_engine->seek(0);
        m_frozenPositionUs = 0;
        m_audioFinished = false;
        m_lastFramePtsUs = -1;
        stopAudio();
        notifyPosition(0, true);
    }
    if (state() == QMediaPlayer::StoppedState)
        resetCurrentLoop();

    stateChanged(QMediaPlayer::PlayingState);
    if (!m_opened) {
        m_playRequested = true;
        return;
    }
    startPlayback();
    mediaStatusChanged(QMediaPlayer::BufferedMedia);
}

void QOS2MediaPlayer::pause()
{
    if (mediaStatus() == QMediaPlayer::NoMedia || mediaStatus() == QMediaPlayer::InvalidMedia)
        return;
    if (state() == QMediaPlayer::PausedState)
        return;

    if (!m_opened) {
        m_playRequested = false;
        m_pauseRequested = true;
        stateChanged(QMediaPlayer::PausedState);
        return;
    }

    m_frozenPositionUs = currentPositionUs();
    if (m_audioSink)
        m_audioSink->suspend();
    m_systemClockRunning = false;
    stateChanged(QMediaPlayer::PausedState);
    if (mediaStatus() == QMediaPlayer::EndOfMedia)
        mediaStatusChanged(QMediaPlayer::BufferedMedia);
    notifyPosition(m_frozenPositionUs, true);
    if (m_lastFramePtsUs < 0)
        m_needFrame = true;
}

void QOS2MediaPlayer::stop()
{
    if (state() == QMediaPlayer::StoppedState && mediaStatus() != QMediaPlayer::EndOfMedia)
        return;

    m_playRequested = false;
    m_pauseRequested = false;
    stopAudio();
    m_systemClockRunning = false;
    stateChanged(QMediaPlayer::StoppedState);
    if (m_opened) {
        m_engine->seek(0);
        m_frozenPositionUs = 0;
        m_clockBaseUs = 0;
        m_audioFinished = false;
        m_lastFramePtsUs = -1;
        m_needFrame = true;
        m_tickTimer.start();
        mediaStatusChanged(QMediaPlayer::LoadedMedia);
    }
    notifyPosition(0, true);
}

void QOS2MediaPlayer::setPosition(qint64 position)
{
    if (!m_opened || !m_engine) {
        m_frozenPositionUs = qMax<qint64>(0, position) * 1000;
        return;
    }
    qint64 ms = qMax<qint64>(0, position);
    if (duration() > 0)
        ms = qMin(ms, duration());
    const qint64 us = ms * 1000;

    m_engine->seek(us);
    stopAudio();
    m_frozenPositionUs = us;
    m_clockBaseUs = us;
    m_systemClockRunning = false;
    m_audioFinished = false;
    m_lastFramePtsUs = -1;
    m_needFrame = true;

    if (mediaStatus() == QMediaPlayer::EndOfMedia)
        mediaStatusChanged(state() == QMediaPlayer::PlayingState ? QMediaPlayer::BufferedMedia
                                                                  : QMediaPlayer::LoadedMedia);
    if (state() == QMediaPlayer::PlayingState)
        startPlayback();
    m_tickTimer.start();
    notifyPosition(us, true);
}

void QOS2MediaPlayer::presentFrame(QOS2FFmpegVideoFrame &frame)
{
    if (!m_videoSink || !frame.frame)
        return;
    const qint64 pts = frame.ptsUs;
    const qint64 end = frame.ptsUs + frame.durationUs;
    auto *buffer = new QOS2FFmpegVideoBuffer(frame.takeFrame());
    const QVideoFrameFormat format = buffer->format();
    if (format.pixelFormat() == QVideoFrameFormat::Format_Invalid) {
        delete buffer;
        return;
    }
    QVideoFrame videoFrame(buffer, format);
    videoFrame.setStartTime(pts);
    videoFrame.setEndTime(end);
    m_videoSink->setVideoFrame(videoFrame);
    m_lastFramePtsUs = end;
}

void QOS2MediaPlayer::notifyPosition(qint64 positionUs, bool force)
{
    const qint64 ms = qMax<qint64>(0, positionUs / 1000);
    if (!force && m_lastNotifiedMs >= 0 && qAbs(ms - m_lastNotifiedMs) < PositionNotifyIntervalMs)
        return;
    m_lastNotifiedMs = ms;
    positionChanged(duration() > 0 ? qMin(ms, duration()) : ms);
}

void QOS2MediaPlayer::handleEndOfMedia()
{
    if (doLoop()) {
        setPosition(0);
        return;
    }
    stopAudio();
    m_systemClockRunning = false;
    m_frozenPositionUs = m_info.durationUs > 0 ? m_info.durationUs : currentPositionUs();
    stateChanged(QMediaPlayer::StoppedState);
    mediaStatusChanged(QMediaPlayer::EndOfMedia);
    notifyPosition(m_frozenPositionUs, true);
    m_tickTimer.stop();
}

void QOS2MediaPlayer::onTick()
{
    if (!m_engine || !m_opened)
        return;

    const bool playing = state() == QMediaPlayer::PlayingState;

    // Audio clock housekeeping
    if (playing && audioEnabled()) {
        if (m_audioSink && !m_audioFinished) {
            const bool starved = !m_audioDevice->hasPendingData() && m_engine->queuedAudioUs() == 0
                    && m_audioSink->state() == QAudio::IdleState;
            const qint64 audioPos = currentPositionUs();
            const bool stalled = starved && m_audioDevice->hasBase()
                    && (m_engine->atEnd() || m_engine->queuedVideoFrames() > 0);
            if (stalled) {
                // Audio ended (or this part of the file has none): keep
                // going on the system clock so video doesn't freeze.
                if (!m_systemClockRunning) {
                    m_clockBaseUs = audioPos;
                    m_systemClock.start();
                    m_systemClockRunning = true;
                } else if (m_engine->atEnd()
                           || m_systemClock.nsecsElapsed() / 1000 > AudioStarvationUs) {
                    m_clockBaseUs = audioPos;
                    m_systemClock.start();
                    m_audioFinished = true;
                    stopAudio();
                }
            } else {
                m_systemClockRunning = false;
            }
        } else if (m_audioFailed) {
            // No usable output: discard decoded audio so it can't stall decoding
            QOS2FFmpegAudioChunk chunk;
            while (m_engine->takeAudio(&chunk)) { }
        } else if (m_audioFinished && !m_engine->atEnd() && m_engine->queuedAudioUs() > 0) {
            // Audio is back: resynchronise on it
            m_frozenPositionUs = currentPositionUs();
            m_audioFinished = false;
            m_systemClockRunning = false;
            startAudio();
        }
    }

    const qint64 pos = currentPositionUs();

    // Video
    if (videoEnabled()) {
        QOS2FFmpegVideoFrame frame;
        qint64 pts = 0;
        if (!playing) {
            if (m_needFrame && m_engine->takeVideo(&frame)) {
                presentFrame(frame);
                m_needFrame = false;
            }
        } else {
            bool have = false;
            while (m_engine->peekVideo(&pts) && (pts <= pos || m_needFrame)) {
                m_engine->takeVideo(&frame);
                have = true;
                m_needFrame = false;
            }
            if (have)
                presentFrame(frame);
        }
    } else {
        m_needFrame = false;
    }

    if (!playing) {
        if (!m_needFrame)
            m_tickTimer.stop();
        return;
    }

    notifyPosition(pos);

    // End of media
    if (m_engine->atEnd() && m_engine->queuedVideoFrames() == 0
        && m_engine->queuedAudioUs() == 0 && !m_audioDevice->hasPendingData()) {
        const bool audioDone = !m_audioSink || m_audioFinished
                || m_audioSink->state() == QAudio::IdleState;
        const bool videoDone = !videoEnabled() || pos >= m_lastFramePtsUs;
        if (audioDone && videoDone)
            handleEndOfMedia();
    }
}

// --- tracks -------------------------------------------------------------------

int QOS2MediaPlayer::trackCount(TrackType type)
{
    switch (type) {
    case AudioStream:
        return m_info.audioTracks.size();
    case VideoStream:
        return m_info.videoTracks.size();
    case SubtitleStream:
        return m_info.subtitleTracks.size();
    default:
        return 0;
    }
}

QMediaMetaData QOS2MediaPlayer::trackMetaData(TrackType type, int streamNumber)
{
    const QList<QOS2FFmpegTrackInfo> *tracks = nullptr;
    switch (type) {
    case AudioStream: tracks = &m_info.audioTracks; break;
    case VideoStream: tracks = &m_info.videoTracks; break;
    case SubtitleStream: tracks = &m_info.subtitleTracks; break;
    default: return {};
    }
    if (streamNumber < 0 || streamNumber >= tracks->size())
        return {};

    const QOS2FFmpegTrackInfo &t = tracks->at(streamNumber);
    QMediaMetaData md;
    if (!t.title.isEmpty())
        md.insert(QMediaMetaData::Title, t.title);
    if (!t.language.isEmpty()) {
        const QLocale::Language lang = QtMultimediaPrivate::fromIso639(t.language.toLatin1().constData());
        if (lang != QLocale::AnyLanguage)
            md.insert(QMediaMetaData::Language, QVariant::fromValue(lang));
    }
    if (type == AudioStream && t.bitRate > 0)
        md.insert(QMediaMetaData::AudioBitRate, int(t.bitRate));
    if (type == VideoStream) {
        md.insert(QMediaMetaData::Resolution, t.size);
        if (t.frameRate > 0)
            md.insert(QMediaMetaData::VideoFrameRate, t.frameRate);
        if (t.bitRate > 0)
            md.insert(QMediaMetaData::VideoBitRate, int(t.bitRate));
    }
    return md;
}

int QOS2MediaPlayer::activeTrack(TrackType type)
{
    switch (type) {
    case AudioStream: return m_activeAudio;
    case VideoStream: return m_activeVideo;
    default: return -1;
    }
}

void QOS2MediaPlayer::setActiveTrack(TrackType type, int streamNumber)
{
    int *active = nullptr;
    int count = 0;
    switch (type) {
    case AudioStream: active = &m_activeAudio; count = m_info.audioTracks.size(); break;
    case VideoStream: active = &m_activeVideo; count = m_info.videoTracks.size(); break;
    default:
        qCDebug(qLcOS2Player) << "Subtitle tracks are not supported";
        return;
    }
    if (streamNumber < -1 || streamNumber >= count || *active == streamNumber)
        return;
    *active = streamNumber;
    activeTracksChanged();
    applyTracks(currentPositionUs());
}

QMediaMetaData QOS2MediaPlayer::metaData() const
{
    return m_metaData;
}

QT_END_NAMESPACE

#include "moc_qos2mediaplayer_p.cpp"
