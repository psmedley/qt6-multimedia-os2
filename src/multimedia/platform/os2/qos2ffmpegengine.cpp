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


#include "qos2ffmpegengine_p.h"

#include <QtCore/qfile.h>
#include <QtCore/qiodevice.h>
#include <QtCore/qloggingcategory.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// FFmpeg 5.1 replaced channel_layout/channels with AVChannelLayout
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#define QOS2_FFMPEG_CH_LAYOUT 1
#endif
// FFmpeg 6.0 added AVFrame::duration (pkt_duration deprecated)
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 2, 100)
#define QOS2_FFMPEG_FRAME_DURATION 1
#endif

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcOS2FFmpeg, "qt.multimedia.os2.ffmpeg")

namespace {

// Queue limits. Decoding continues while any active stream is below its
// "high" mark, and stops unconditionally when a hard cap is reached.
constexpr qint64 AudioHighUs = 1000000;
constexpr qint64 AudioHardCapUs = 4000000;
constexpr int VideoHigh = 6;
constexpr int VideoHardCap = 24;
constexpr int IoBufferSize = 64 * 1024;

QString avErrorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QString dictValue(AVDictionary *dict, const char *key)
{
    AVDictionaryEntry *e = av_dict_get(dict, key, nullptr, 0);
    return e ? QString::fromUtf8(e->value) : QString();
}

int frameChannels(const AVFrame *frame)
{
#ifdef QOS2_FFMPEG_CH_LAYOUT
    return frame->ch_layout.nb_channels;
#else
    return frame->channels;
#endif
}

int codecChannels(const AVCodecParameters *par)
{
#ifdef QOS2_FFMPEG_CH_LAYOUT
    return par->ch_layout.nb_channels;
#else
    return par->channels;
#endif
}

// Pixel formats QVideoFrame can display directly
bool isDirectPixelFormat(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_RGB0:
        return true;
    default:
        return false;
    }
}

} // namespace

// --- QOS2FFmpegVideoFrame ---------------------------------------------------

QOS2FFmpegVideoFrame::QOS2FFmpegVideoFrame(QOS2FFmpegVideoFrame &&other) noexcept
    : frame(other.frame),
      ptsUs(other.ptsUs),
      durationUs(other.durationUs),
      serial(other.serial)
{
    other.frame = nullptr;
}

QOS2FFmpegVideoFrame &QOS2FFmpegVideoFrame::operator=(QOS2FFmpegVideoFrame &&other) noexcept
{
    if (this != &other) {
        av_frame_free(&frame);
        frame = other.frame;
        other.frame = nullptr;
        ptsUs = other.ptsUs;
        durationUs = other.durationUs;
        serial = other.serial;
    }
    return *this;
}

QOS2FFmpegVideoFrame::~QOS2FFmpegVideoFrame()
{
    av_frame_free(&frame);
}

AVFrame *QOS2FFmpegVideoFrame::takeFrame()
{
    AVFrame *f = frame;
    frame = nullptr;
    return f;
}

// --- QOS2FFmpegEngine -------------------------------------------------------

QOS2FFmpegEngine::QOS2FFmpegEngine(QObject *parent)
    : QThread(parent)
{
}

QOS2FFmpegEngine::~QOS2FFmpegEngine()
{
    abort();
    wait();
    closeInput();
}

void QOS2FFmpegEngine::openMedia(const QUrl &url, QIODevice *stream)
{
    Q_ASSERT(!isRunning());
    m_url = url;
    m_stream = stream;
    m_abort.storeRelaxed(0);
    start();
}

void QOS2FFmpegEngine::abort()
{
    QMutexLocker locker(&m_mutex);
    m_abort.storeRelease(1);
    m_wait.wakeAll();
}

QOS2FFmpegMediaInfo QOS2FFmpegEngine::mediaInfo() const
{
    QMutexLocker locker(&m_mutex);
    return m_info;
}

void QOS2FFmpegEngine::startDecoding(int audioTrack, int videoTrack, int outRate,
                                     int outChannels, qint64 startUs)
{
    QMutexLocker locker(&m_mutex);
    m_audioTrack = audioTrack;
    m_videoTrack = videoTrack;
    m_outRate = outRate;
    m_outChannels = outChannels;
    m_tracksPending = true;
    if (startUs > 0) {
        m_seekPending = true;
        m_seekTargetUs = startUs;
    }
    m_started = true;
    m_wait.wakeAll();
}

int QOS2FFmpegEngine::setTracks(int audioTrack, int videoTrack, int outRate, int outChannels,
                                qint64 positionUs)
{
    QMutexLocker locker(&m_mutex);
    m_audioTrack = audioTrack;
    m_videoTrack = videoTrack;
    m_outRate = outRate;
    m_outChannels = outChannels;
    m_tracksPending = true;
    m_seekPending = true;
    m_seekTargetUs = qMax<qint64>(0, positionUs);
    ++m_serial;
    m_audioQueue.clear();
    m_audioQueuedBytes = 0;
    m_videoQueue.clear();
    m_eof = false;
    m_wait.wakeAll();
    return m_serial;
}

int QOS2FFmpegEngine::seek(qint64 positionUs)
{
    QMutexLocker locker(&m_mutex);
    m_seekPending = true;
    m_seekTargetUs = qMax<qint64>(0, positionUs);
    ++m_serial;
    m_audioQueue.clear();
    m_audioQueuedBytes = 0;
    m_videoQueue.clear();
    m_eof = false;
    m_wait.wakeAll();
    return m_serial;
}

int QOS2FFmpegEngine::serial() const
{
    QMutexLocker locker(&m_mutex);
    return m_serial;
}

bool QOS2FFmpegEngine::takeAudio(QOS2FFmpegAudioChunk *chunk)
{
    QMutexLocker locker(&m_mutex);
    while (!m_audioQueue.empty()) {
        QOS2FFmpegAudioChunk front = std::move(m_audioQueue.front());
        m_audioQueue.pop_front();
        m_audioQueuedBytes -= front.data.size();
        if (front.serial != m_serial)
            continue;
        *chunk = std::move(front);
        m_wait.wakeAll();
        return true;
    }
    return false;
}

qint64 QOS2FFmpegEngine::queuedAudioUs() const
{
    QMutexLocker locker(&m_mutex);
    const qint64 bytesPerSec = qint64(m_outRate) * m_outChannels * 2;
    return bytesPerSec > 0 ? m_audioQueuedBytes * 1000000 / bytesPerSec : 0;
}

bool QOS2FFmpegEngine::peekVideo(qint64 *ptsUs, qint64 *durationUs) const
{
    QMutexLocker locker(&m_mutex);
    for (const auto &f : m_videoQueue) {
        if (f.serial != m_serial)
            continue;
        *ptsUs = f.ptsUs;
        if (durationUs)
            *durationUs = f.durationUs;
        return true;
    }
    return false;
}

bool QOS2FFmpegEngine::takeVideo(QOS2FFmpegVideoFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    while (!m_videoQueue.empty()) {
        QOS2FFmpegVideoFrame front = std::move(m_videoQueue.front());
        m_videoQueue.pop_front();
        if (front.serial != m_serial)
            continue;
        *frame = std::move(front);
        m_wait.wakeAll();
        return true;
    }
    return false;
}

int QOS2FFmpegEngine::queuedVideoFrames() const
{
    QMutexLocker locker(&m_mutex);
    return int(m_videoQueue.size());
}

bool QOS2FFmpegEngine::atEnd() const
{
    QMutexLocker locker(&m_mutex);
    return m_eof && m_decodeSerial == m_serial && !m_seekPending && !m_tracksPending;
}

// --- decoding thread ----------------------------------------------------------

int QOS2FFmpegEngine::interruptCallback(void *opaque)
{
    return static_cast<QOS2FFmpegEngine *>(opaque)->m_abort.loadAcquire();
}

int QOS2FFmpegEngine::ioRead(void *opaque, uint8_t *buf, int size)
{
    auto *self = static_cast<QOS2FFmpegEngine *>(opaque);
    if (!self->m_stream)
        return AVERROR(EIO);
    const qint64 got = self->m_stream->read(reinterpret_cast<char *>(buf), size);
    if (got < 0)
        return AVERROR(EIO);
    if (got == 0)
        return AVERROR_EOF;
    return int(got);
}

int64_t QOS2FFmpegEngine::ioSeek(void *opaque, int64_t offset, int whence)
{
    auto *self = static_cast<QOS2FFmpegEngine *>(opaque);
    QIODevice *dev = self->m_stream;
    if (!dev)
        return -1;
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE:
        return dev->isSequential() ? -1 : dev->size();
    case SEEK_SET:
        break;
    case SEEK_CUR:
        offset += dev->pos();
        break;
    case SEEK_END:
        if (dev->isSequential())
            return -1;
        offset += dev->size();
        break;
    default:
        return -1;
    }
    if (dev->isSequential() || !dev->seek(offset))
        return -1;
    return dev->pos();
}

bool QOS2FFmpegEngine::openInput(QString *error)
{
    m_format = avformat_alloc_context();
    if (!m_format) {
        *error = QStringLiteral("Out of memory");
        return false;
    }
    m_format->interrupt_callback.callback = &QOS2FFmpegEngine::interruptCallback;
    m_format->interrupt_callback.opaque = this;

    QByteArray location;
    if (m_stream) {
        auto *buffer = static_cast<unsigned char *>(av_malloc(IoBufferSize));
        m_io = avio_alloc_context(buffer, IoBufferSize, 0, this, &QOS2FFmpegEngine::ioRead,
                                  nullptr, &QOS2FFmpegEngine::ioSeek);
        if (!m_io) {
            av_free(buffer);
            *error = QStringLiteral("Out of memory");
            return false;
        }
        m_io->seekable = m_stream->isSequential() ? 0 : AVIO_SEEKABLE_NORMAL;
        m_format->pb = m_io;
        m_format->flags |= AVFMT_FLAG_CUSTOM_IO;
    } else if (m_url.isLocalFile()) {
        location = QFile::encodeName(m_url.toLocalFile());
    } else {
        location = m_url.toEncoded();
    }

    int err = avformat_open_input(&m_format, location.isEmpty() ? nullptr : location.constData(),
                                  nullptr, nullptr);
    if (err < 0) {
        // avformat_open_input frees the context on failure
        m_format = nullptr;
        *error = avErrorString(err);
        return false;
    }
    err = avformat_find_stream_info(m_format, nullptr);
    if (err < 0) {
        *error = avErrorString(err);
        return false;
    }
    m_startTimeUs = (m_format->start_time != AV_NOPTS_VALUE) ? m_format->start_time : 0;
    return true;
}

void QOS2FFmpegEngine::closeInput()
{
    closeDecoder(&m_audio);
    closeDecoder(&m_video);
    swr_free(&m_swr);
    sws_freeContext(m_sws);
    m_sws = nullptr;
    if (m_format)
        avformat_close_input(&m_format);
    if (m_io) {
        av_freep(&m_io->buffer);
        avio_context_free(&m_io);
    }
}

void QOS2FFmpegEngine::collectInfo()
{
    QOS2FFmpegMediaInfo info;
    info.durationUs = (m_format->duration != AV_NOPTS_VALUE) ? m_format->duration : 0;
    info.seekable = info.durationUs > 0
            && !(m_format->iformat->flags & AVFMT_NOTIMESTAMPS)
            && (!m_format->pb || (m_format->pb->seekable & AVIO_SEEKABLE_NORMAL));
    info.formatName = QString::fromUtf8(m_format->iformat->name);
    info.bitRate = m_format->bit_rate;

    AVDictionaryEntry *entry = nullptr;
    while ((entry = av_dict_get(m_format->metadata, "", entry, AV_DICT_IGNORE_SUFFIX)))
        info.metadata.insert(QByteArray(entry->key).toLower(), QString::fromUtf8(entry->value));

    for (unsigned i = 0; i < m_format->nb_streams; ++i) {
        AVStream *st = m_format->streams[i];
        const AVCodecParameters *par = st->codecpar;
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            if (info.coverArt.isEmpty() && st->attached_pic.size > 0)
                info.coverArt = QByteArray(reinterpret_cast<const char *>(st->attached_pic.data),
                                           st->attached_pic.size);
            continue;
        }
        QOS2FFmpegTrackInfo track;
        track.streamIndex = int(i);
        track.language = dictValue(st->metadata, "language");
        track.title = dictValue(st->metadata, "title");
        track.codec = QString::fromUtf8(avcodec_get_name(par->codec_id));
        track.bitRate = par->bit_rate;
        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            track.sampleRate = par->sample_rate;
            track.channels = codecChannels(par);
            info.audioTracks.append(track);
            break;
        case AVMEDIA_TYPE_VIDEO:
            track.size = QSize(par->width, par->height);
            if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0)
                track.frameRate = av_q2d(st->avg_frame_rate);
            info.videoTracks.append(track);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            info.subtitleTracks.append(track);
            break;
        default:
            break;
        }
    }

    auto trackFor = [](const QList<QOS2FFmpegTrackInfo> &tracks, int streamIndex) {
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks.at(i).streamIndex == streamIndex)
                return i;
        return tracks.isEmpty() ? -1 : 0;
    };
    const int bestVideo = av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int bestAudio = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, bestVideo, nullptr, 0);
    info.defaultVideoTrack = trackFor(info.videoTracks, bestVideo);
    info.defaultAudioTrack = trackFor(info.audioTracks, bestAudio);

    QMutexLocker locker(&m_mutex);
    m_info = info;
}

bool QOS2FFmpegEngine::openDecoder(Decoder *decoder, int streamIndex)
{
    closeDecoder(decoder);
    if (streamIndex < 0)
        return false;

    AVStream *st = m_format->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        emit decodeError(QStringLiteral("No decoder for codec %1")
                                 .arg(QString::fromUtf8(avcodec_get_name(st->codecpar->codec_id))));
        return false;
    }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return false;
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->pkt_timebase = st->time_base;
    ctx->thread_count = 0; // automatic
    int err = avcodec_open2(ctx, codec, nullptr);
    if (err < 0) {
        emit decodeError(QStringLiteral("Unable to open decoder: %1").arg(avErrorString(err)));
        avcodec_free_context(&ctx);
        return false;
    }
    decoder->ctx = ctx;
    decoder->streamIndex = streamIndex;
    st->discard = AVDISCARD_DEFAULT;
    qCDebug(qLcOS2FFmpeg) << "Opened decoder" << codec->name << "for stream" << streamIndex;
    return true;
}

void QOS2FFmpegEngine::closeDecoder(Decoder *decoder)
{
    if (decoder->ctx)
        avcodec_free_context(&decoder->ctx);
    decoder->streamIndex = -1;
}

void QOS2FFmpegEngine::applyTracks(int audioTrack, int videoTrack)
{
    QOS2FFmpegMediaInfo info = mediaInfo();
    const int audioStream = (audioTrack >= 0 && audioTrack < info.audioTracks.size())
            ? info.audioTracks.at(audioTrack).streamIndex : -1;
    const int videoStream = (videoTrack >= 0 && videoTrack < info.videoTracks.size())
            ? info.videoTracks.at(videoTrack).streamIndex : -1;

    if (audioStream != m_audio.streamIndex)
        openDecoder(&m_audio, audioStream);
    // The output format may have changed too
    swr_free(&m_swr);
    if (videoStream != m_video.streamIndex)
        openDecoder(&m_video, videoStream);

    // Don't waste time demuxing streams we don't use
    for (unsigned i = 0; i < m_format->nb_streams; ++i) {
        const int idx = int(i);
        m_format->streams[i]->discard = (idx == m_audio.streamIndex || idx == m_video.streamIndex)
                ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }
}

void QOS2FFmpegEngine::flushDecoders()
{
    if (m_audio.ctx)
        avcodec_flush_buffers(m_audio.ctx);
    if (m_video.ctx)
        avcodec_flush_buffers(m_video.ctx);
    if (m_swr) {
        // Drop any samples buffered inside the resampler
        swr_free(&m_swr);
    }
}

void QOS2FFmpegEngine::doSeek(qint64 positionUs)
{
    const qint64 ts = positionUs + m_startTimeUs;
    int err = avformat_seek_file(m_format, -1, INT64_MIN, ts, ts, 0);
    if (err < 0)
        err = av_seek_frame(m_format, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (err < 0)
        qCWarning(qLcOS2FFmpeg) << "Seek to" << positionUs << "failed:" << avErrorString(err);
    flushDecoders();
    m_skipUntilUs = positionUs;
    m_nextAudioPtsUs = positionUs;
}

qint64 QOS2FFmpegEngine::toUs(qint64 ts, int streamIndex) const
{
    if (ts == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;
    return av_rescale_q(ts, m_format->streams[streamIndex]->time_base, AV_TIME_BASE_Q)
            - m_startTimeUs;
}

bool QOS2FFmpegEngine::queuesSatisfied() const
{
    // m_mutex must be held
    const qint64 bytesPerSec = qint64(m_outRate) * m_outChannels * 2;
    const qint64 audioUs = bytesPerSec > 0 ? m_audioQueuedBytes * 1000000 / bytesPerSec : 0;
    const int videoFrames = int(m_videoQueue.size());
    const bool hasAudio = m_audio.ctx != nullptr;
    const bool hasVideo = m_video.ctx != nullptr;

    if ((hasAudio && audioUs >= AudioHardCapUs) || (hasVideo && videoFrames >= VideoHardCap))
        return true;
    const bool audioOk = !hasAudio || audioUs >= AudioHighUs;
    const bool videoOk = !hasVideo || videoFrames >= VideoHigh;
    return audioOk && videoOk;
}

bool QOS2FFmpegEngine::ensureResampler(AVFrame *frame)
{
#ifdef QOS2_FFMPEG_CH_LAYOUT
    AVChannelLayout inLayout;
    if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || frame->ch_layout.nb_channels <= 0)
        av_channel_layout_default(&inLayout, qMax(1, frame->ch_layout.nb_channels));
    else
        av_channel_layout_copy(&inLayout, &frame->ch_layout);
    const quint64 layoutKey = (quint64(inLayout.nb_channels) << 32)
            ^ (inLayout.order == AV_CHANNEL_ORDER_NATIVE ? inLayout.u.mask : 0);
#else
    int64_t inLayout = frame->channel_layout;
    if (!inLayout || av_get_channel_layout_nb_channels(inLayout) != frame->channels)
        inLayout = av_get_default_channel_layout(frame->channels);
    const quint64 layoutKey = quint64(inLayout);
#endif

    if (m_swr && m_swrInRate == frame->sample_rate && m_swrInFormat == frame->format
        && m_swrInLayoutKey == layoutKey) {
#ifdef QOS2_FFMPEG_CH_LAYOUT
        av_channel_layout_uninit(&inLayout);
#endif
        return true;
    }

    swr_free(&m_swr);
    int outRate, outChannels;
    {
        QMutexLocker locker(&m_mutex);
        outRate = m_outRate;
        outChannels = m_outChannels;
    }

#ifdef QOS2_FFMPEG_CH_LAYOUT
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, outChannels);
    int err = swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_S16, outRate, &inLayout,
                                  AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);
    if (err < 0)
        swr_free(&m_swr);
#else
    m_swr = swr_alloc_set_opts(nullptr, av_get_default_channel_layout(outChannels),
                               AV_SAMPLE_FMT_S16, outRate, inLayout,
                               AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr);
#endif
    if (!m_swr || swr_init(m_swr) < 0) {
        swr_free(&m_swr);
        emit decodeError(QStringLiteral("Unable to set up audio resampler"));
        return false;
    }
    m_swrInRate = frame->sample_rate;
    m_swrInFormat = frame->format;
    m_swrInLayoutKey = layoutKey;
    return true;
}

void QOS2FFmpegEngine::handleAudioFrame(AVFrame *frame)
{
    if (frame->sample_rate <= 0 || frameChannels(frame) <= 0 || !ensureResampler(frame))
        return;

    qint64 ptsUs = toUs(frame->best_effort_timestamp, m_audio.streamIndex);
    if (ptsUs == AV_NOPTS_VALUE)
        ptsUs = m_nextAudioPtsUs;
    m_nextAudioPtsUs = ptsUs + qint64(frame->nb_samples) * 1000000 / frame->sample_rate;

    int outRate, outChannels;
    {
        QMutexLocker locker(&m_mutex);
        outRate = m_outRate;
        outChannels = m_outChannels;
    }
    const int maxOut = int(av_rescale_rnd(swr_get_delay(m_swr, frame->sample_rate) + frame->nb_samples,
                                          outRate, frame->sample_rate, AV_ROUND_UP));
    const int bytesPerFrame = outChannels * 2;
    QByteArray data(qsizetype(maxOut) * bytesPerFrame, Qt::Uninitialized);
    uint8_t *out = reinterpret_cast<uint8_t *>(data.data());
    const int got = swr_convert(m_swr, &out, maxOut,
                                const_cast<const uint8_t **>(frame->extended_data),
                                frame->nb_samples);
    if (got <= 0)
        return;
    data.resize(qsizetype(got) * bytesPerFrame);

    // Accurate seek: drop audio before the target
    if (m_skipUntilUs >= 0) {
        const qint64 endUs = ptsUs + qint64(got) * 1000000 / outRate;
        if (endUs <= m_skipUntilUs)
            return;
        if (ptsUs < m_skipUntilUs) {
            const qint64 skipFrames = (m_skipUntilUs - ptsUs) * outRate / 1000000;
            data.remove(0, qsizetype(skipFrames) * bytesPerFrame);
            ptsUs = m_skipUntilUs;
        }
    }

    QMutexLocker locker(&m_mutex);
    if (m_decodeSerial != m_serial || m_seekPending)
        return;
    m_audioQueuedBytes += data.size();
    m_audioQueue.push_back({ std::move(data), ptsUs, m_decodeSerial });
}

AVFrame *QOS2FFmpegEngine::convertVideoFrame(AVFrame *frame)
{
    AVFrame *out = av_frame_alloc();
    if (!out)
        return nullptr;
    out->format = AV_PIX_FMT_YUV420P;
    out->width = frame->width;
    out->height = frame->height;
    if (av_frame_get_buffer(out, 0) < 0) {
        av_frame_free(&out);
        return nullptr;
    }
    m_sws = sws_getCachedContext(m_sws, frame->width, frame->height, AVPixelFormat(frame->format),
                                 out->width, out->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);
    if (!m_sws) {
        av_frame_free(&out);
        return nullptr;
    }
    sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, out->data, out->linesize);
    av_frame_copy_props(out, frame);
    return out;
}

void QOS2FFmpegEngine::handleVideoFrame(AVFrame *frame)
{
    AVStream *st = m_format->streams[m_video.streamIndex];
    const qint64 ptsUs = toUs(frame->best_effort_timestamp, m_video.streamIndex);
    if (ptsUs == AV_NOPTS_VALUE)
        return;

#ifdef QOS2_FFMPEG_FRAME_DURATION
    qint64 durationUs = toUs(frame->duration, m_video.streamIndex) + m_startTimeUs;
#else
    qint64 durationUs = toUs(frame->pkt_duration, m_video.streamIndex) + m_startTimeUs;
#endif
    if (durationUs <= 0 && st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0)
        durationUs = qint64(1000000 / av_q2d(st->avg_frame_rate));
    if (durationUs <= 0)
        durationUs = 40000;

    if (m_skipUntilUs >= 0 && ptsUs + durationUs <= m_skipUntilUs)
        return;

    AVFrame *out;
    if (isDirectPixelFormat(frame->format)) {
        out = av_frame_clone(frame);
    } else {
        out = convertVideoFrame(frame);
    }
    if (!out)
        return;

    QOS2FFmpegVideoFrame item;
    item.frame = out;
    item.ptsUs = ptsUs;
    item.durationUs = durationUs;
    item.serial = m_decodeSerial;

    QMutexLocker locker(&m_mutex);
    if (m_decodeSerial != m_serial || m_seekPending)
        return;
    m_videoQueue.push_back(std::move(item));
}

void QOS2FFmpegEngine::decodePacket(Decoder *decoder, AVPacket *packet)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return;

    auto receiveAll = [&]() {
        for (;;) {
            const int r = avcodec_receive_frame(decoder->ctx, frame);
            if (r < 0)
                break;
            if (decoder == &m_audio)
                handleAudioFrame(frame);
            else
                handleVideoFrame(frame);
            av_frame_unref(frame);
            if (m_abort.loadAcquire())
                break;
        }
    };

    int err = avcodec_send_packet(decoder->ctx, packet);
    if (err == AVERROR(EAGAIN)) {
        receiveAll();
        err = avcodec_send_packet(decoder->ctx, packet);
    }
    if (err < 0 && err != AVERROR_EOF && err != AVERROR(EAGAIN))
        qCDebug(qLcOS2FFmpeg) << "Decoding error:" << avErrorString(err);
    receiveAll();

    // The first frames after an accurate seek have been delivered
    if (m_skipUntilUs >= 0) {
        QMutexLocker locker(&m_mutex);
        const bool videoReached = !m_video.ctx || !m_videoQueue.empty();
        const bool audioReached = !m_audio.ctx || !m_audioQueue.empty();
        if (videoReached && audioReached)
            m_skipUntilUs = -1;
    }
    av_frame_free(&frame);
}

void QOS2FFmpegEngine::run()
{
    QString error;
    if (!openInput(&error)) {
        qCWarning(qLcOS2FFmpeg) << "Unable to open" << m_url << ":" << error;
        closeInput();
        if (!m_abort.loadAcquire())
            emit openFailed(error);
        return;
    }
    collectInfo();
    emit mediaOpened();

    AVPacket *packet = av_packet_alloc();
    bool eofSignalled = false;

    for (;;) {
        bool tracksPending, seekPending;
        int audioTrack, videoTrack;
        qint64 seekTarget;
        {
            QMutexLocker locker(&m_mutex);
            while (!m_abort.loadAcquire()
                   && (!m_started
                       || (!m_seekPending && !m_tracksPending && (m_eof || queuesSatisfied())))) {
                m_wait.wait(&m_mutex);
            }
            if (m_abort.loadAcquire())
                break;
            tracksPending = m_tracksPending;
            seekPending = m_seekPending;
            audioTrack = m_audioTrack;
            videoTrack = m_videoTrack;
            seekTarget = m_seekTargetUs;
            m_tracksPending = false;
            m_seekPending = false;
            m_decodeSerial = m_serial;
            if (seekPending)
                m_eof = false;
        }

        if (tracksPending)
            applyTracks(audioTrack, videoTrack);
        if (seekPending) {
            doSeek(seekTarget);
            eofSignalled = false;
        }

        if (!m_audio.ctx && !m_video.ctx) {
            QMutexLocker locker(&m_mutex);
            m_eof = true;
            continue;
        }

        const int err = av_read_frame(m_format, packet);
        if (err < 0) {
            if (err != AVERROR_EOF && !avio_feof(m_format->pb))
                qCWarning(qLcOS2FFmpeg) << "Read error:" << avErrorString(err);
            // Drain the decoders
            if (m_audio.ctx)
                decodePacket(&m_audio, nullptr);
            if (m_video.ctx)
                decodePacket(&m_video, nullptr);
            m_skipUntilUs = -1;
            {
                QMutexLocker locker(&m_mutex);
                if (m_decodeSerial == m_serial && !m_seekPending)
                    m_eof = true;
            }
            if (!eofSignalled) {
                eofSignalled = true;
                emit endOfStream();
            }
            continue;
        }

        if (packet->stream_index == m_audio.streamIndex)
            decodePacket(&m_audio, packet);
        else if (packet->stream_index == m_video.streamIndex)
            decodePacket(&m_video, packet);
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    closeInput();
}

QT_END_NAMESPACE

#include "moc_qos2ffmpegengine_p.cpp"
