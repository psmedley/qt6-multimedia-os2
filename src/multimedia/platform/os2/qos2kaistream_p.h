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


#ifndef QOS2KAISTREAM_P_H
#define QOS2KAISTREAM_P_H

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
#define INCL_DOS
#define INCL_OS2MM
#include <os2.h>
#include <os2me.h>
#include <kai.h>

#include "qos2audiostream_p.h"

QT_BEGIN_NAMESPACE

// Audio stream on top of KO Myung-Hun's K Audio Interface (libkai), which
// drives either UniAud directly or DART, and can software-mix several
// streams onto one device.
class QOS2KaiStream : public QOS2AudioStream
{
public:
    QOS2KaiStream(Direction direction, QOS2AudioRingBuffer *ring);
    ~QOS2KaiStream() override;

    bool open(unsigned short deviceIndex, const QAudioFormat &format) override;
    bool start() override;
    void close() override;
    bool pause() override;
    bool resume() override;
    bool isOpen() const override { return m_handle != 0; }

    static bool initialize();
    static void shutdown();
    static bool supportsRecording();
    static QList<QPair<unsigned short, QString>> availableDevices();

private:
    static ULONG APIENTRY callback(PVOID data, PVOID buffer, ULONG size);
    ULONG handleCallback(char *buffer, ULONG size);

    HKAI m_handle = 0;
};

QT_END_NAMESPACE

#endif // QOS2KAISTREAM_P_H
