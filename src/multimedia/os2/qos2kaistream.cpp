// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2kaistream_p.h"

#include <QtCore/qloggingcategory.h>

#include <string.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(qLcOS2Kai, "qt.multimedia.os2.kai")

QOS2KaiStream::QOS2KaiStream(Direction direction, QOS2AudioRingBuffer *ring)
    : QOS2AudioStream(direction, ring)
{
}

QOS2KaiStream::~QOS2KaiStream()
{
    close();
}

bool QOS2KaiStream::initialize()
{
    // KAIM_AUTO prefers UniAud and falls back to DART. KAI itself honours
    // KAI_AUTOMODE=DART|UNIAUD to force a particular mode, and its software
    // mixer (which lets several streams share one device) is controlled with
    // KAI_NOSOFTMIXER.
    if (kaiInit(KAIM_AUTO) != 0)
        return false;

    KAICAPS caps;
    memset(&caps, 0, sizeof(caps));
    if (kaiCaps(&caps) == 0) {
        qCDebug(qLcOS2Kai) << "KAI mode" << (caps.ulMode == KAIM_UNIAUD ? "UniAud" : "DART")
                           << "device" << caps.szPDDName << "max channels" << caps.ulMaxChannels;
    }
    return true;
}

void QOS2KaiStream::shutdown()
{
    kaiDone();
}

bool QOS2KaiStream::supportsRecording()
{
#ifdef KAIT_RECORD
    return true;
#else
    return false;
#endif
}

bool QOS2KaiStream::open(unsigned short deviceIndex, const QAudioFormat &format)
{
    close();

    int bitsPerSample = 0;
    if (!silenceFor(format, &bitsPerSample, &m_silence)) {
        qCWarning(qLcOS2Kai) << "Unsupported sample format" << format.sampleFormat();
        return false;
    }

    KAISPEC wanted;
    KAISPEC obtained;
    memset(&wanted, 0, sizeof(wanted));
    memset(&obtained, 0, sizeof(obtained));
    wanted.usDeviceIndex = deviceIndex;
#ifdef KAIT_RECORD
    wanted.ulType = (m_direction == Playback) ? KAIT_PLAY : KAIT_RECORD;
#else
    wanted.ulType = KAIT_PLAY;
#endif
    wanted.ulBitsPerSample = bitsPerSample;
    wanted.ulSamplingRate = format.sampleRate();
    wanted.ulDataFormat = MCI_WAVE_FORMAT_PCM;
    wanted.ulChannels = format.channelCount();
    wanted.ulNumBuffers = m_bufferCount;
    wanted.ulBufferSize = preferredBufferBytes(format);
    wanted.fShareable = TRUE;
    wanted.pfnCallBack = reinterpret_cast<PFNKAICB>(&QOS2KaiStream::callback);
    wanted.pCallBackData = this;

    if (kaiOpen(&wanted, &obtained, &m_handle) != 0) {
        qCWarning(qLcOS2Kai) << "kaiOpen failed for" << format;
        m_handle = 0;
        return false;
    }

    // Qt expects the exact format it asked for; KAI may hand back something else
    if (obtained.ulSamplingRate != wanted.ulSamplingRate
        || obtained.ulChannels != wanted.ulChannels
        || obtained.ulBitsPerSample != wanted.ulBitsPerSample) {
        qCWarning(qLcOS2Kai) << "KAI could not provide" << format << "- got"
                             << obtained.ulSamplingRate << "Hz" << obtained.ulChannels << "ch"
                             << obtained.ulBitsPerSample << "bits";
        kaiClose(m_handle);
        m_handle = 0;
        return false;
    }

    m_silence = char(obtained.bSilence);
    m_bufferBytes = obtained.ulBufferSize;
    m_bufferCount = obtained.ulNumBuffers;
    m_stopping.storeRelaxed(0);
    m_xruns.storeRelaxed(0);
    resetProcessedBytes();

    qCDebug(qLcOS2Kai) << "Opened KAI device" << deviceIndex
                       << (m_direction == Playback ? "playback" : "record") << format
                       << "buffers" << m_bufferCount << "x" << m_bufferBytes;
    return true;
}

bool QOS2KaiStream::start()
{
    if (!m_handle)
        return false;
    if (kaiPlay(m_handle) != 0) {
        qCWarning(qLcOS2Kai) << "kaiPlay failed";
        return false;
    }
    return true;
}

void QOS2KaiStream::close()
{
    if (!m_handle)
        return;

    m_stopping.storeRelease(1);
    kaiStop(m_handle);
    // Wait for a callback that may still be executing
    { QMutexLocker locker(&m_callbackMutex); }
    kaiClose(m_handle);
    m_handle = 0;
}

bool QOS2KaiStream::pause()
{
    return m_handle && kaiPause(m_handle) == 0;
}

bool QOS2KaiStream::resume()
{
    return m_handle && kaiResume(m_handle) == 0;
}

ULONG APIENTRY QOS2KaiStream::callback(PVOID data, PVOID buffer, ULONG size)
{
    auto *self = static_cast<QOS2KaiStream *>(data);
    if (!self || !buffer)
        return 0;
    return self->handleCallback(static_cast<char *>(buffer), size);
}

ULONG QOS2KaiStream::handleCallback(char *buffer, ULONG size)
{
    QMutexLocker locker(&m_callbackMutex);

    if (m_direction == Playback) {
        // Always hand KAI a full buffer: returning less than requested makes
        // KAI treat the stream as finished.
        if (m_stopping.loadAcquire())
            memset(buffer, m_silence, size);
        else
            fillPlayback(buffer, size);
        return size;
    }

    if (!m_stopping.loadAcquire())
        storeCaptured(buffer, size);
    return size;
}

QList<QPair<unsigned short, QString>> QOS2KaiStream::availableDevices()
{
    QList<QPair<unsigned short, QString>> devices;

#if defined(QT_KAI_HAS_CARD_API)
    const ULONG count = kaiGetCardCount();
    for (ULONG i = 1; i <= count; ++i) {
        KAICAPS caps;
        memset(&caps, 0, sizeof(caps));
        QString description;
        if (kaiCapsEx(i, &caps) == 0 && caps.szPDDName[0])
            description = QString::fromLocal8Bit(caps.szPDDName).trimmed();
        if (description.isEmpty())
            description = QStringLiteral("Audio device %1").arg(i);
        devices.append({ static_cast<unsigned short>(i), description });
    }
#else
    // Older KAI releases have no kaiGetCardCount()/kaiCapsEx(), so only the
    // default device (index 0, always listed by QOS2MediaDevices) is usable.
#endif
    return devices;
}

QT_END_NAMESPACE
