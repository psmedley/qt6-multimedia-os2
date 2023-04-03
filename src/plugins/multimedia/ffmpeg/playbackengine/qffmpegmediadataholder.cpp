// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "playbackengine/qffmpegmediadataholder_p.h"

#include "qffmpegmediametadata_p.h"
#include "qffmpegmediaformatinfo_p.h"
#include "qiodevice.h"
#include "qdatetime.h"
#include "qloggingcategory.h"

#include <optional>

QT_BEGIN_NAMESPACE

static Q_LOGGING_CATEGORY(qLcMediaDataHolder, "qt.multimedia.ffmpeg.mediadataholder")

namespace QFFmpeg {

static std::optional<qint64> streamDuration(const AVStream &stream)
{
    const auto &factor = stream.time_base;

    if (stream.duration > 0 && factor.num > 0 && factor.den > 0) {
        return qint64(1000000) * stream.duration * factor.num / factor.den;
    }

    // In some cases ffmpeg reports negative duration that is definitely invalid.
    // However, the correct duration may be read from the metadata.

    if (stream.duration < 0) {
        qCWarning(qLcMediaDataHolder) << "AVStream duration" << stream.duration
                                      << "is invalid. Taking it from the metadata";
    }

    if (const auto duration = av_dict_get(stream.metadata, "DURATION", nullptr, 0)) {
        const auto time = QTime::fromString(QString::fromUtf8(duration->value));
        return qint64(1000) * time.msecsSinceStartOfDay();
    }

    return {};
}

static void insertMediaData(QMediaMetaData &metaData, QPlatformMediaPlayer::TrackType trackType,
                            const AVStream *stream)
{
    Q_ASSERT(stream);
    const auto *codecPar = stream->codecpar;

    switch (trackType) {
    case QPlatformMediaPlayer::VideoStream:
        metaData.insert(QMediaMetaData::VideoBitRate, (int)codecPar->bit_rate);
        metaData.insert(QMediaMetaData::VideoCodec,
                        QVariant::fromValue(QFFmpegMediaFormatInfo::videoCodecForAVCodecId(
                                codecPar->codec_id)));
        metaData.insert(QMediaMetaData::Resolution, QSize(codecPar->width, codecPar->height));
        metaData.insert(QMediaMetaData::VideoFrameRate,
                        qreal(stream->avg_frame_rate.num) / qreal(stream->avg_frame_rate.den));
        break;
    case QPlatformMediaPlayer::AudioStream:
        metaData.insert(QMediaMetaData::AudioBitRate, (int)codecPar->bit_rate);
        metaData.insert(QMediaMetaData::AudioCodec,
                        QVariant::fromValue(QFFmpegMediaFormatInfo::audioCodecForAVCodecId(
                                codecPar->codec_id)));
        break;
    default:
        break;
    }
};

static int readQIODevice(void *opaque, uint8_t *buf, int buf_size)
{
    auto *dev = static_cast<QIODevice *>(opaque);
    if (dev->atEnd())
        return AVERROR_EOF;
    return dev->read(reinterpret_cast<char *>(buf), buf_size);
}

static int64_t seekQIODevice(void *opaque, int64_t offset, int whence)
{
    QIODevice *dev = static_cast<QIODevice *>(opaque);

    if (dev->isSequential())
        return AVERROR(EINVAL);

    if (whence & AVSEEK_SIZE)
        return dev->size();

    whence &= ~AVSEEK_FORCE;

    if (whence == SEEK_CUR)
        offset += dev->pos();
    else if (whence == SEEK_END)
        offset += dev->size();

    if (!dev->seek(offset))
        return AVERROR(EINVAL);
    return offset;
}

QPlatformMediaPlayer::TrackType MediaDataHolder::trackTypeFromMediaType(int mediaType)
{
    switch (mediaType) {
    case AVMEDIA_TYPE_AUDIO:
        return QPlatformMediaPlayer::AudioStream;
    case AVMEDIA_TYPE_VIDEO:
        return QPlatformMediaPlayer::VideoStream;
    case AVMEDIA_TYPE_SUBTITLE:
        return QPlatformMediaPlayer::SubtitleStream;
    default:
        return QPlatformMediaPlayer::NTrackTypes;
    }
}

std::optional<MediaDataHolder::ContextError>
MediaDataHolder::recreateAVFormatContext(const QUrl &media, QIODevice *stream)
{
    *this = MediaDataHolder{};

    QByteArray url = media.toEncoded(QUrl::PreferLocalFile);

    AVFormatContext *context = nullptr;

    if (stream) {
        if (!stream->isOpen()) {
            if (!stream->open(QIODevice::ReadOnly))
                return ContextError{ QMediaPlayer::ResourceError,
                                     QLatin1String("Could not open source device.") };
        }
        if (!stream->isSequential())
            stream->seek(0);
        context = avformat_alloc_context();

        constexpr int bufferSize = 32768;
        unsigned char *buffer = (unsigned char *)av_malloc(bufferSize);
        context->pb = avio_alloc_context(buffer, bufferSize, false, stream, &readQIODevice, nullptr, &seekQIODevice);
    }

    int ret = avformat_open_input(&context, url.constData(), nullptr, nullptr);
    if (ret < 0) {
        auto code = QMediaPlayer::ResourceError;
        if (ret == AVERROR(EACCES))
            code = QMediaPlayer::AccessDeniedError;
        else if (ret == AVERROR(EINVAL))
            code = QMediaPlayer::FormatError;

        return ContextError{ code, QMediaPlayer::tr("Could not open file") };
    }

    ret = avformat_find_stream_info(context, nullptr);
    if (ret < 0) {
        avformat_close_input(&context);
        return ContextError{ QMediaPlayer::FormatError,
                             QMediaPlayer::tr("Could not find stream information for media file") };
    }

#ifndef QT_NO_DEBUG
    av_dump_format(context, 0, url.constData(), 0);
#endif

    m_isSeekable = !(context->ctx_flags & AVFMTCTX_UNSEEKABLE);
    m_context.reset(context);

    updateStreams();
    updateMetaData();

    return {};
}

void MediaDataHolder::updateStreams()
{
    m_duration = 0;
    m_requestedStreams = { -1, -1, -1 };
    m_currentAVStreamIndex = { -1, -1, -1 };
    m_streamMap = {};

    if (!m_context) {
        return;
    }

    for (unsigned int i = 0; i < m_context->nb_streams; ++i) {

        const auto *stream = m_context->streams[i];
        const auto trackType = trackTypeFromMediaType(stream->codecpar->codec_type);

        if (trackType == QPlatformMediaPlayer::NTrackTypes)
            continue;

        auto metaData = QFFmpegMetaData::fromAVMetaData(stream->metadata);
        const bool isDefault = stream->disposition & AV_DISPOSITION_DEFAULT;

        if (trackType != QPlatformMediaPlayer::SubtitleStream) {
            insertMediaData(metaData, trackType, stream);

            if (isDefault && m_requestedStreams[trackType] < 0)
                m_requestedStreams[trackType] = m_streamMap[trackType].size();
        }

        if (auto duration = streamDuration(*stream)) {
            m_duration = qMax(m_duration, *duration);
            metaData.insert(QMediaMetaData::Duration, *duration / qint64(1000));
        }

        m_streamMap[trackType].append({ (int)i, isDefault, metaData });
    }

    for (auto trackType :
         { QPlatformMediaPlayer::VideoStream, QPlatformMediaPlayer::AudioStream }) {
        auto &requestedStream = m_requestedStreams[trackType];
        auto &streamMap = m_streamMap[trackType];

        if (requestedStream < 0 && !streamMap.empty())
            requestedStream = 0;

        if (requestedStream >= 0)
            m_currentAVStreamIndex[trackType] = streamMap[requestedStream].avStreamIndex;
    }
}

void MediaDataHolder::updateMetaData()
{
    m_metaData = {};

    if (!m_context)
        return;

    m_metaData = QFFmpegMetaData::fromAVMetaData(m_context->metadata);
    m_metaData.insert(QMediaMetaData::FileFormat,
                      QVariant::fromValue(QFFmpegMediaFormatInfo::fileFormatForAVInputFormat(
                              m_context->iformat)));
    m_metaData.insert(QMediaMetaData::Duration, m_duration / qint64(1000));

    for (auto trackType :
         { QPlatformMediaPlayer::AudioStream, QPlatformMediaPlayer::VideoStream }) {
        const auto streamIndex = m_currentAVStreamIndex[trackType];
        if (streamIndex >= 0)
            insertMediaData(m_metaData, trackType, m_context->streams[streamIndex]);
    }
}

bool MediaDataHolder::setActiveTrack(QPlatformMediaPlayer::TrackType type, int streamNumber)
{
    if (!m_context)
        return false;

    if (streamNumber < 0 || streamNumber >= m_streamMap[type].size())
        streamNumber = -1;
    if (m_requestedStreams[type] == streamNumber)
        return false;
    m_requestedStreams[type] = streamNumber;
    const int avStreamIndex = m_streamMap[type].value(streamNumber).avStreamIndex;

    const int oldIndex = m_currentAVStreamIndex[type];
    qCDebug(qLcMediaDataHolder) << ">>>>> change track" << type << "from" << oldIndex << "to"
                                << avStreamIndex;

    // TODO: maybe add additional verifications
    m_currentAVStreamIndex[type] = avStreamIndex;

    updateMetaData();

    return true;
}

int MediaDataHolder::activeTrack(QPlatformMediaPlayer::TrackType type) const
{
    return type < QPlatformMediaPlayer::NTrackTypes ? m_requestedStreams[type] : -1;
}

const QList<MediaDataHolder::StreamInfo> &MediaDataHolder::streamInfo(
        QPlatformMediaPlayer::TrackType trackType) const
{
    Q_ASSERT(trackType < QPlatformMediaPlayer::NTrackTypes);

    return m_streamMap[trackType];
}

} // namespace QFFmpeg

QT_END_NAMESPACE
