// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2audiodevices_p.h"
#include "qos2audiodevice_p.h"
#include "qos2audiosink_p.h"
#include "qos2audiosource_p.h"
#include "qos2audiostream_p.h"

QT_BEGIN_NAMESPACE

QOS2AudioDevices::QOS2AudioDevices()
    : QPlatformAudioDevices()
{
    QOS2AudioStream::initializeBackend();
}

QOS2AudioDevices::~QOS2AudioDevices()
{
    QOS2AudioStream::shutdownBackend();
}

QLatin1String QOS2AudioDevices::backendName() const
{
    return QOS2AudioStream::backend(QOS2AudioStream::Playback) == QOS2AudioStream::Kai
            ? QLatin1String{ "OS/2 KAI" }
            : QLatin1String{ "OS/2 DART" };
}

static QList<QAudioDevice> availableDevices(QAudioDevice::Mode mode)
{
    QList<QAudioDevice> devices;

    // Ordinal 0 always refers to the system default device.
    devices.append(QAudioDevicePrivate::createQAudioDevice(std::make_unique<QOS2AudioDeviceInfo>(
            QByteArrayLiteral("0"), QStringLiteral("Default audio device"), mode)));

    const auto direction = (mode == QAudioDevice::Input) ? QOS2AudioStream::Record
                                                         : QOS2AudioStream::Playback;
    const auto installed = QOS2AudioStream::availableDevices(direction);
    for (const auto &dev : installed) {
        devices.append(QAudioDevicePrivate::createQAudioDevice(std::make_unique<QOS2AudioDeviceInfo>(
                QByteArray::number(dev.first), dev.second, mode)));
    }
    return devices;
}

QList<QAudioDevice> QOS2AudioDevices::findAudioInputs() const
{
    return availableDevices(QAudioDevice::Input);
}

QList<QAudioDevice> QOS2AudioDevices::findAudioOutputs() const
{
    return availableDevices(QAudioDevice::Output);
}

QPlatformAudioSource *QOS2AudioDevices::createAudioSource(const QAudioDevice &deviceInfo,
                                                          const QAudioFormat &format,
                                                          QObject *parent)
{
    return new QOS2AudioSource(deviceInfo, format, parent);
}

QPlatformAudioSink *QOS2AudioDevices::createAudioSink(const QAudioDevice &deviceInfo,
                                                      const QAudioFormat &format, QObject *parent)
{
    return new QOS2AudioSink(deviceInfo, format, parent);
}

QT_END_NAMESPACE
