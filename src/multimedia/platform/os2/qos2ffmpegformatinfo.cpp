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


#include "qos2ffmpegformatinfo_p.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

QT_BEGIN_NAMESPACE

namespace {

struct FileFormatEntry { QMediaFormat::FileFormat format; const char *demuxer; bool audioOnly; };
const FileFormatEntry fileFormats[] = {
    { QMediaFormat::WMV, "asf", false },
    { QMediaFormat::AVI, "avi", false },
    { QMediaFormat::Matroska, "matroska", false },
    { QMediaFormat::MPEG4, "mov", false },
    { QMediaFormat::Ogg, "ogg", false },
    { QMediaFormat::QuickTime, "mov", false },
    { QMediaFormat::WebM, "matroska", false },
    { QMediaFormat::Mpeg4Audio, "mov", true },
    { QMediaFormat::AAC, "aac", true },
    { QMediaFormat::WMA, "asf", true },
    { QMediaFormat::MP3, "mp3", true },
    { QMediaFormat::FLAC, "flac", true },
    { QMediaFormat::Wave, "wav", true },
};

struct AudioCodecEntry { QMediaFormat::AudioCodec codec; AVCodecID id; };
const AudioCodecEntry audioCodecs[] = {
    { QMediaFormat::AudioCodec::MP3, AV_CODEC_ID_MP3 },
    { QMediaFormat::AudioCodec::AAC, AV_CODEC_ID_AAC },
    { QMediaFormat::AudioCodec::AC3, AV_CODEC_ID_AC3 },
    { QMediaFormat::AudioCodec::EAC3, AV_CODEC_ID_EAC3 },
    { QMediaFormat::AudioCodec::FLAC, AV_CODEC_ID_FLAC },
    { QMediaFormat::AudioCodec::DolbyTrueHD, AV_CODEC_ID_TRUEHD },
    { QMediaFormat::AudioCodec::Opus, AV_CODEC_ID_OPUS },
    { QMediaFormat::AudioCodec::Vorbis, AV_CODEC_ID_VORBIS },
    { QMediaFormat::AudioCodec::Wave, AV_CODEC_ID_PCM_S16LE },
    { QMediaFormat::AudioCodec::WMA, AV_CODEC_ID_WMAV2 },
    { QMediaFormat::AudioCodec::ALAC, AV_CODEC_ID_ALAC },
};

struct VideoCodecEntry { QMediaFormat::VideoCodec codec; AVCodecID id; };
const VideoCodecEntry videoCodecs[] = {
    { QMediaFormat::VideoCodec::MPEG1, AV_CODEC_ID_MPEG1VIDEO },
    { QMediaFormat::VideoCodec::MPEG2, AV_CODEC_ID_MPEG2VIDEO },
    { QMediaFormat::VideoCodec::MPEG4, AV_CODEC_ID_MPEG4 },
    { QMediaFormat::VideoCodec::H264, AV_CODEC_ID_H264 },
    { QMediaFormat::VideoCodec::H265, AV_CODEC_ID_HEVC },
    { QMediaFormat::VideoCodec::VP8, AV_CODEC_ID_VP8 },
    { QMediaFormat::VideoCodec::VP9, AV_CODEC_ID_VP9 },
    { QMediaFormat::VideoCodec::AV1, AV_CODEC_ID_AV1 },
    { QMediaFormat::VideoCodec::Theora, AV_CODEC_ID_THEORA },
    { QMediaFormat::VideoCodec::WMV, AV_CODEC_ID_WMV3 },
    { QMediaFormat::VideoCodec::MotionJPEG, AV_CODEC_ID_MJPEG },
};

} // namespace

QOS2FFmpegFormatInfo::QOS2FFmpegFormatInfo()
{
    QList<QMediaFormat::AudioCodec> audio;
    for (const auto &e : audioCodecs)
        if (avcodec_find_decoder(e.id))
            audio.append(e.codec);
    QList<QMediaFormat::VideoCodec> video;
    for (const auto &e : videoCodecs)
        if (avcodec_find_decoder(e.id))
            video.append(e.codec);

    // FFmpeg containers accept practically any codec, so pair every
    // available container with every available decoder.
    for (const auto &e : fileFormats) {
        if (!av_find_input_format(e.demuxer))
            continue;
        decoders.append({ e.format, audio, e.audioOnly ? QList<QMediaFormat::VideoCodec>() : video });
    }
}

QT_END_NAMESPACE
