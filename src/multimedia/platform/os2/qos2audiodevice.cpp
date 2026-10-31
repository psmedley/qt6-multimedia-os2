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


#include "qos2audiodevice_p.h"

QT_BEGIN_NAMESPACE

QOS2AudioDeviceInfo::QOS2AudioDeviceInfo(const QByteArray &id, const QString &desc,
                                         QAudioDevice::Mode mode)
    : QAudioDevicePrivate(id, mode)
{
    description = desc;
    isDefault = (deviceIndex(id) == 0);

    // DART on top of the common OS/2 audio drivers (UniAud, SB16 etc.)
    // reliably supports 8 bit unsigned and 16 bit signed PCM, mono/stereo.
    minimumChannelCount = 1;
    maximumChannelCount = 2;
    minimumSampleRate = 8000;
    maximumSampleRate = 48000;
    supportedSampleFormats << QAudioFormat::UInt8 << QAudioFormat::Int16;

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

QT_END_NAMESPACE
