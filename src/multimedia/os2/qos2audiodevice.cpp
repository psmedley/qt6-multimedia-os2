// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2audiodevice_p.h"

QT_BEGIN_NAMESPACE

QOS2AudioDeviceInfo::QOS2AudioDeviceInfo(const QByteArray &id, const QString &desc,
                                         QAudioDevice::Mode mode)
    : QAudioDevicePrivate(id, mode, desc)
{
    isDefault = (deviceIndex(id) == 0);

    // DART/KAI on top of the common OS/2 audio drivers (UniAud, SB16 etc.)
    // reliably support mono/stereo at 8-48 kHz. Int32 and Float samples are
    // converted to/from 16 bit by the sink and source.
    minimumChannelCount = 1;
    maximumChannelCount = 2;
    minimumSampleRate = 8000;
    maximumSampleRate = 48000;
    supportedSampleFormats = {
        QAudioFormat::UInt8,
        QAudioFormat::Int16,
        QAudioFormat::Int32,
        QAudioFormat::Float,
    };
    preferredFormat.setChannelCount(2);
    preferredFormat.setSampleRate(44100);
    preferredFormat.setSampleFormat(QAudioFormat::Int16);
}

QOS2AudioDeviceInfo::~QOS2AudioDeviceInfo() = default;

unsigned short QOS2AudioDeviceInfo::deviceIndex(const QByteArray &id)
{
    bool ok = false;
    const uint index = id.toUInt(&ok);
    return (ok && index <= 0xffff) ? static_cast<unsigned short>(index) : 0;
}

QAudioFormat::SampleFormat QOS2AudioDeviceInfo::hostSampleFormat(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return QAudioFormat::UInt8;
    case QAudioFormat::Int16:
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        return QAudioFormat::Int16;
    default:
        return QAudioFormat::Unknown;
    }
}

bool QOS2AudioDeviceInfo::isSupportedFormat(const QAudioFormat &format)
{
    return format.isValid() && hostSampleFormat(format.sampleFormat()) != QAudioFormat::Unknown
        && format.channelCount() >= 1 && format.channelCount() <= 2;
}

QT_END_NAMESPACE
