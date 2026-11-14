// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QOS2DARTSTREAM_P_H
#define QOS2DARTSTREAM_P_H

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

#include "qos2audiostream_p.h"

QT_BEGIN_NAMESPACE

// Audio stream on top of the MMPM/2 DART (Direct Audio Real-Time) mixer.
class QOS2DartStream : public QOS2AudioStream
{
public:
    QOS2DartStream(Direction direction, QOS2AudioRingBuffer *ring);
    ~QOS2DartStream() override;

    bool open(unsigned short deviceIndex, const QAudioFormat &format) override;
    bool start() override;
    void close() override;
    bool pause() override;
    bool resume() override;
    bool isOpen() const override { return m_deviceId != 0; }

    static QList<QPair<unsigned short, QString>> availableDevices();

private:
    static LONG APIENTRY mixEvent(ULONG status, PMCI_MIX_BUFFER buffer, ULONG flags);
    void handleEvent(PMCI_MIX_BUFFER buffer, ULONG flags);
    void setupRecordConnector();

    USHORT m_deviceId = 0;
    bool m_mixInitialized = false;
    bool m_buffersAllocated = false;
    MCI_MIXSETUP_PARMS m_mixSetup;
    QList<MCI_MIX_BUFFER> m_buffers;
};

QT_END_NAMESPACE

#endif // QOS2DARTSTREAM_P_H
