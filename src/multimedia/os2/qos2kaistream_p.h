// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

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
