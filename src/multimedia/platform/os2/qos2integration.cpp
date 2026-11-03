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


#include <private/qtmultimediaglobal_p.h>

#include "qos2integration_p.h"
#include "qos2mediadevices_p.h"
#include "qos2audiostream_p.h"

#include "qos2audiooutput_p.h"
#include "qos2videosink_p.h"
#if QT_CONFIG(os2ffmpeg)
#include "qos2ffmpegformatinfo_p.h"
#include "qos2mediaplayer_p.h"
#endif

#include <private/qplatformmediaformatinfo_p.h>

QT_BEGIN_NAMESPACE

QOS2Integration::QOS2Integration()
{
    QOS2AudioStream::initializeBackend();
}

QOS2Integration::~QOS2Integration()
{
    delete m_devices;
    delete m_formatInfo;
    QOS2AudioStream::shutdownBackend();
}

QPlatformMediaDevices *QOS2Integration::devices()
{
    if (!m_devices)
        m_devices = new QOS2MediaDevices();
    return m_devices;
}

QPlatformMediaFormatInfo *QOS2Integration::formatInfo()
{
    if (!m_formatInfo) {
#if QT_CONFIG(os2ffmpeg)
        m_formatInfo = new QOS2FFmpegFormatInfo();
#else
        // Raw PCM audio input/output only: no encoders or decoders
        m_formatInfo = new QPlatformMediaFormatInfo();
#endif
    }
    return m_formatInfo;
}

QPlatformAudioOutput *QOS2Integration::createAudioOutput(QAudioOutput *q)
{
    return new QOS2AudioOutput(q);
}

QPlatformVideoSink *QOS2Integration::createVideoSink(QVideoSink *sink)
{
    return new QOS2VideoSink(sink);
}

QPlatformMediaPlayer *QOS2Integration::createPlayer(QMediaPlayer *player)
{
#if QT_CONFIG(os2ffmpeg)
    return new QOS2MediaPlayer(player);
#else
    Q_UNUSED(player);
    return nullptr;
#endif
}

QT_END_NAMESPACE
