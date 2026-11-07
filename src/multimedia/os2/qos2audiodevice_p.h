// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QOS2AUDIODEVICE_P_H
#define QOS2AUDIODEVICE_P_H

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

#include <QtMultimedia/private/qaudiodevice_p.h>

QT_BEGIN_NAMESPACE

class QOS2AudioDeviceInfo : public QAudioDevicePrivate
{
public:
    QOS2AudioDeviceInfo(const QByteArray &id, const QString &description, QAudioDevice::Mode mode);
    Q_DISABLE_COPY_MOVE(QOS2AudioDeviceInfo)
    ~QOS2AudioDeviceInfo() override;

    // Converts a device id as created by QOS2AudioDevices into the
    // device ordinal used by DART/KAI (0 = system default device).
    static unsigned short deviceIndex(const QByteArray &id);

    // DART/KAI natively handle 8 bit unsigned and 16 bit signed PCM. Other
    // sample formats are converted to/from 16 bit by the sink and source.
    static QAudioFormat::SampleFormat hostSampleFormat(QAudioFormat::SampleFormat format);
    static bool isSupportedFormat(const QAudioFormat &format);
};

QT_END_NAMESPACE

#endif // QOS2AUDIODEVICE_P_H
