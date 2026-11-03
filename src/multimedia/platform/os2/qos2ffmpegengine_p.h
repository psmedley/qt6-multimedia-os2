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


#ifndef QOS2FFMPEGENGINE_P_H
#define QOS2FFMPEGENGINE_P_H

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
#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qmap.h>
#include <QtCore/qmutex.h>
#include <QtCore/qpointer.h>
#include <QtCore/qsize.h>
#include <QtCore/qstring.h>
#include <QtCore/qthread.h>
#include <QtCore/qurl.h>
#include <QtCore/qwaitcondition.h>

#include <cstdint>
#include <deque>

extern "C" {
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVIOContext;
struct AVPacket;
struct SwrContext;
struct SwsContext;
}

QT_BEGIN_NAMESPACE

class QIODevice;

// A decoded, interleaved signed 16 bit PCM chunk
struct QOS2FFmpegAudioChunk
{
    QByteArray data;
    qint64 ptsUs = 0;
    int serial = 0;
};

// A decoded video picture. Owns a reference to the AVFrame.
struct QOS2FFmpegVideoFrame
{
    QOS2FFmpegVideoFrame() = default;
    QOS2FFmpegVideoFrame(QOS2FFmpegVideoFrame &&other) noexcept;
    QOS2FFmpegVideoFrame &operator=(QOS2FFmpegVideoFrame &&other) noexcept;
    ~QOS2FFmpegVideoFrame();
    Q_DISABLE_COPY(QOS2FFmpegVideoFrame)

    AVFrame *takeFrame();

    AVFrame *frame = nullptr;
    qint64 ptsUs = 0;
    qint64 durationUs = 0;
    int serial = 0;
};

struct QOS2FFmpegTrackInfo
{
    int streamIndex = -1;
    QString language;
    QString title;
    QString codec;
    qint64 bitRate = 0;
    // audio
    int sampleRate = 0;
    int channels = 0;
    // video
    QSize size;
    double frameRate = 0;
};

struct QOS2FFmpegMediaInfo
{
    qint64 durationUs = 0;
    bool seekable = false;
    QString formatName;
    qint64 bitRate = 0;
    QMap<QByteArray, QString> metadata;
    QList<QOS2FFmpegTrackInfo> audioTracks;
    QList<QOS2FFmpegTrackInfo> videoTracks;
    QList<QOS2FFmpegTrackInfo> subtitleTracks;
    int defaultAudioTrack = -1;
    int defaultVideoTrack = -1;
    QByteArray coverArt; // encoded image (e.g. JPEG) if the file embeds one
};

// Demuxes and decodes a media file or stream on its own thread using FFmpeg.
//
// Usage: openMedia() -> mediaOpened() (or openFailed()) -> startDecoding().
// Decoded audio and video accumulate in bounded queues which the consumer
// drains with takeAudio()/takeVideo(). seek() discards everything queued and
// bumps the serial number so stale data can be recognised.
class QOS2FFmpegEngine : public QThread
{
    Q_OBJECT
public:
    explicit QOS2FFmpegEngine(QObject *parent = nullptr);
    ~QOS2FFmpegEngine() override;

    void openMedia(const QUrl &url, QIODevice *stream);
    void abort();

    QOS2FFmpegMediaInfo mediaInfo() const;

    // Track numbers index into mediaInfo().audioTracks/videoTracks (-1 = off).
    // The audio is converted to outRate/outChannels S16 PCM.
    void startDecoding(int audioTrack, int videoTrack, int outRate, int outChannels,
                       qint64 startUs = 0);
    // Switches tracks and/or the audio output format; decoding restarts at
    // positionUs. Returns the new serial.
    int setTracks(int audioTrack, int videoTrack, int outRate, int outChannels,
                  qint64 positionUs);
    // Returns the new serial
    int seek(qint64 positionUs);
    int serial() const;

    bool takeAudio(QOS2FFmpegAudioChunk *chunk);
    qint64 queuedAudioUs() const;

    bool peekVideo(qint64 *ptsUs, qint64 *durationUs = nullptr) const;
    bool takeVideo(QOS2FFmpegVideoFrame *frame);
    int queuedVideoFrames() const;

    // True once the current serial has been fully decoded
    bool atEnd() const;

Q_SIGNALS:
    void mediaOpened();
    void openFailed(const QString &error);
    void decodeError(const QString &error);
    void endOfStream();

protected:
    void run() override;

private:
    struct Decoder
    {
        AVCodecContext *ctx = nullptr;
        int streamIndex = -1;
    };

    bool openInput(QString *error);
    void closeInput();
    void collectInfo();
    bool openDecoder(Decoder *decoder, int streamIndex);
    void closeDecoder(Decoder *decoder);
    void applyTracks(int audioTrack, int videoTrack);
    void doSeek(qint64 positionUs);
    void flushDecoders();
    bool queuesSatisfied() const;
    void decodePacket(Decoder *decoder, AVPacket *packet);
    void handleAudioFrame(AVFrame *frame);
    void handleVideoFrame(AVFrame *frame);
    bool ensureResampler(AVFrame *frame);
    AVFrame *convertVideoFrame(AVFrame *frame);
    qint64 toUs(qint64 ts, int streamIndex) const;

    static int interruptCallback(void *opaque);
    static int ioRead(void *opaque, uint8_t *buf, int size);
    static int64_t ioSeek(void *opaque, int64_t offset, int whence);

    // Input (only touched by the decoding thread after openMedia())
    QUrl m_url;
    QPointer<QIODevice> m_stream;
    AVFormatContext *m_format = nullptr;
    AVIOContext *m_io = nullptr;
    Decoder m_audio;
    Decoder m_video;
    SwrContext *m_swr = nullptr;
    int m_swrInRate = 0;
    int m_swrInFormat = -1;
    quint64 m_swrInLayoutKey = 0;
    SwsContext *m_sws = nullptr;
    qint64 m_startTimeUs = 0;
    qint64 m_nextAudioPtsUs = 0;
    qint64 m_skipUntilUs = -1; // accurate seek: drop data before this
    int m_decodeSerial = 0;

    // Shared state, protected by m_mutex
    mutable QMutex m_mutex;
    QWaitCondition m_wait;
    QAtomicInt m_abort = 0;
    bool m_started = false;
    bool m_eof = false;
    int m_serial = 0;
    bool m_seekPending = false;
    qint64 m_seekTargetUs = 0;
    bool m_tracksPending = false;
    int m_audioTrack = -1;
    int m_videoTrack = -1;
    int m_outRate = 44100;
    int m_outChannels = 2;
    QOS2FFmpegMediaInfo m_info;
    std::deque<QOS2FFmpegAudioChunk> m_audioQueue;
    qint64 m_audioQueuedBytes = 0;
    std::deque<QOS2FFmpegVideoFrame> m_videoQueue;
};

QT_END_NAMESPACE

#endif // QOS2FFMPEGENGINE_P_H
