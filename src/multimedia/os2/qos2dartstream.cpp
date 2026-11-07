// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qos2dartstream_p.h"
#include "qos2audioringbuffer_p.h"

#include <QtCore/qbytearray.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qvarlengtharray.h>

#include <stdlib.h>
#include <string.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(qLcOS2Dart, "qt.multimedia.os2.dart")

namespace {

inline bool mciFailed(ULONG rc)
{
    return LOUSHORT(rc) != MCIERR_SUCCESS;
}

QString mciErrorString(ULONG rc)
{
    char buf[128];
    if (mciGetErrorString(rc, (PSZ)buf, sizeof(buf)) == MCIERR_SUCCESS)
        return QString::fromLocal8Bit(buf);
    return QString::fromLatin1("MCI error %1").arg(LOUSHORT(rc));
}

} // namespace

QOS2DartStream::QOS2DartStream(Direction direction, QOS2AudioRingBuffer *ring)
    : QOS2AudioStream(direction, ring)
{
    memset(&m_mixSetup, 0, sizeof(m_mixSetup));
}

QOS2DartStream::~QOS2DartStream()
{
    close();
}

bool QOS2DartStream::open(unsigned short deviceIndex, const QAudioFormat &format)
{
    close();

    int bitsPerSample = 0;
    if (!silenceFor(format, &bitsPerSample, &m_silence)) {
        qCWarning(qLcOS2Dart) << "Unsupported sample format" << format.sampleFormat();
        return false;
    }

    // Open the amp-mixer device
    MCI_AMP_OPEN_PARMS openParms;
    memset(&openParms, 0, sizeof(openParms));
    openParms.pszDeviceType = (PSZ)MAKEULONG(MCI_DEVTYPE_AUDIO_AMPMIX, deviceIndex);
    ULONG rc = mciSendCommand(0, MCI_OPEN,
                              MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE,
                              &openParms, 0);
    if (mciFailed(rc)) {
        qCWarning(qLcOS2Dart) << "MCI_OPEN failed:" << mciErrorString(rc);
        return false;
    }
    m_deviceId = openParms.usDeviceID;

    if (m_direction == Record)
        setupRecordConnector();

    // Set up the mixer for the requested format
    memset(&m_mixSetup, 0, sizeof(m_mixSetup));
    m_mixSetup.ulBitsPerSample = bitsPerSample;
    m_mixSetup.ulFormatTag = MCI_WAVE_FORMAT_PCM;
    m_mixSetup.ulSamplesPerSec = format.sampleRate();
    m_mixSetup.ulChannels = format.channelCount();
    m_mixSetup.ulFormatMode = (m_direction == Playback) ? MCI_PLAY : MCI_RECORD;
    m_mixSetup.ulDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    m_mixSetup.pmixEvent = reinterpret_cast<PMIXEREVENT>(&QOS2DartStream::mixEvent);

    rc = mciSendCommand(m_deviceId, MCI_MIXSETUP, MCI_WAIT | MCI_MIXSETUP_INIT, &m_mixSetup, 0);
    if (mciFailed(rc)) {
        qCWarning(qLcOS2Dart) << "MCI_MIXSETUP failed:" << mciErrorString(rc)
                              << format;
        close();
        return false;
    }
    m_mixInitialized = true;

    // Allocate the DART buffers
    m_bufferBytes = preferredBufferBytes(format);

    m_buffers.resize(m_bufferCount);
    memset(m_buffers.data(), 0, m_buffers.size() * sizeof(MCI_MIX_BUFFER));

    MCI_BUFFER_PARMS bufferParms;
    memset(&bufferParms, 0, sizeof(bufferParms));
    bufferParms.ulNumBuffers = m_buffers.size();
    bufferParms.ulBufferSize = m_bufferBytes;
    bufferParms.pBufList = m_buffers.data();
    rc = mciSendCommand(m_deviceId, MCI_BUFFER, MCI_WAIT | MCI_ALLOCATE_MEMORY, &bufferParms, 0);
    if (mciFailed(rc)) {
        qCWarning(qLcOS2Dart) << "MCI_BUFFER allocation failed:" << mciErrorString(rc);
        m_buffers.clear();
        close();
        return false;
    }
    m_buffersAllocated = true;
    // DART may hand back fewer buffers than requested
    m_buffers.resize(qMin<qsizetype>(m_buffers.size(), bufferParms.ulNumBuffers));
    m_bufferCount = m_buffers.size();
    m_bufferBytes = bufferParms.ulBufferSize;

    for (MCI_MIX_BUFFER &buffer : m_buffers) {
        buffer.ulBufferLength = m_bufferBytes;
        buffer.ulFlags = 0;
        buffer.ulUserParm = (ULONG)this;
    }

    m_stopping.storeRelaxed(0);
    m_xruns.storeRelaxed(0);
    resetProcessedBytes();

    qCDebug(qLcOS2Dart) << "Opened DART device" << deviceIndex << "id" << m_deviceId
                        << (m_direction == Playback ? "playback" : "record") << format
                        << "buffers" << m_buffers.size() << "x" << m_bufferBytes;
    return true;
}

bool QOS2DartStream::start()
{
    if (!isOpen() || m_buffers.isEmpty())
        return false;

    ULONG rc;
    if (m_direction == Playback) {
        for (MCI_MIX_BUFFER &buffer : m_buffers) {
            fillPlayback(static_cast<char *>(buffer.pBuffer), m_bufferBytes);
            buffer.ulBufferLength = m_bufferBytes;
        }
        rc = m_mixSetup.pmixWrite(m_mixSetup.ulMixHandle, m_buffers.data(), m_buffers.size());
    } else {
        rc = m_mixSetup.pmixRead(m_mixSetup.ulMixHandle, m_buffers.data(), m_buffers.size());
    }
    if (mciFailed(rc)) {
        qCWarning(qLcOS2Dart) << "Failed to start DART stream:" << mciErrorString(rc);
        return false;
    }
    return true;
}

void QOS2DartStream::close()
{
    if (!m_deviceId)
        return;

    // Make sure the callback won't resubmit any more buffers
    m_stopping.storeRelease(1);

    MCI_GENERIC_PARMS genericParms;
    memset(&genericParms, 0, sizeof(genericParms));
    mciSendCommand(m_deviceId, MCI_STOP, MCI_WAIT, &genericParms, 0);

    // Wait for a callback that may still be executing
    { QMutexLocker locker(&m_callbackMutex); }

    if (m_buffersAllocated) {
        MCI_BUFFER_PARMS bufferParms;
        memset(&bufferParms, 0, sizeof(bufferParms));
        bufferParms.ulNumBuffers = m_buffers.size();
        bufferParms.ulBufferSize = m_bufferBytes;
        bufferParms.pBufList = m_buffers.data();
        mciSendCommand(m_deviceId, MCI_BUFFER, MCI_WAIT | MCI_DEALLOCATE_MEMORY, &bufferParms, 0);
        m_buffersAllocated = false;
    }
    m_buffers.clear();

    if (m_mixInitialized) {
        mciSendCommand(m_deviceId, MCI_MIXSETUP, MCI_WAIT | MCI_MIXSETUP_DEINIT, &m_mixSetup, 0);
        m_mixInitialized = false;
    }

    memset(&genericParms, 0, sizeof(genericParms));
    mciSendCommand(m_deviceId, MCI_CLOSE, MCI_WAIT, &genericParms, 0);
    m_deviceId = 0;
}

bool QOS2DartStream::pause()
{
    if (!isOpen())
        return false;
    MCI_GENERIC_PARMS genericParms;
    memset(&genericParms, 0, sizeof(genericParms));
    return !mciFailed(mciSendCommand(m_deviceId, MCI_PAUSE, MCI_WAIT, &genericParms, 0));
}

bool QOS2DartStream::resume()
{
    if (!isOpen())
        return false;
    MCI_GENERIC_PARMS genericParms;
    memset(&genericParms, 0, sizeof(genericParms));
    return !mciFailed(mciSendCommand(m_deviceId, MCI_RESUME, MCI_WAIT, &genericParms, 0));
}

void QOS2DartStream::setupRecordConnector()
{
    // Select the input connector. Defaults to the microphone; set
    // QT_OS2_AUDIO_INPUT=line to record from line-in instead.
    MCI_CONNECTOR_PARMS connectorParms;
    memset(&connectorParms, 0, sizeof(connectorParms));
    const QByteArray input = qgetenv("QT_OS2_AUDIO_INPUT").toLower();
    connectorParms.ulConnectorType = (input == "line") ? MCI_LINE_IN_CONNECTOR
                                                       : MCI_MICROPHONE_CONNECTOR;
    ULONG rc = mciSendCommand(m_deviceId, MCI_CONNECTOR,
                              MCI_WAIT | MCI_ENABLE_CONNECTOR | MCI_CONNECTOR_TYPE,
                              &connectorParms, 0);
    if (mciFailed(rc))
        qCDebug(qLcOS2Dart) << "Unable to select input connector:" << mciErrorString(rc);

    // Don't route the recorded signal back to the speakers
    MCI_AMP_SET_PARMS ampSetParms;
    memset(&ampSetParms, 0, sizeof(ampSetParms));
    ampSetParms.ulItem = MCI_AMP_SET_MONITOR;
    mciSendCommand(m_deviceId, MCI_SET, MCI_WAIT | MCI_SET_OFF | MCI_SET_ITEM, &ampSetParms, 0);
}

LONG APIENTRY QOS2DartStream::mixEvent(ULONG status, PMCI_MIX_BUFFER buffer, ULONG flags)
{
    Q_UNUSED(status);
    if (!buffer)
        return TRUE;
    auto *self = reinterpret_cast<QOS2DartStream *>(buffer->ulUserParm);
    if (self)
        self->handleEvent(buffer, flags);
    return TRUE;
}

void QOS2DartStream::handleEvent(PMCI_MIX_BUFFER buffer, ULONG flags)
{
    QMutexLocker locker(&m_callbackMutex);
    if (m_stopping.loadAcquire())
        return;

    if (flags & MIX_STREAM_ERROR) {
        // Underrun (playback) or overrun (record) reported by the driver
        m_xruns.ref();
    }

    if (m_direction == Playback && (flags & MIX_WRITE_COMPLETE)) {
        fillPlayback(static_cast<char *>(buffer->pBuffer), m_bufferBytes);
        buffer->ulBufferLength = m_bufferBytes;
        m_mixSetup.pmixWrite(m_mixSetup.ulMixHandle, buffer, 1);
    } else if (m_direction == Record && (flags & MIX_READ_COMPLETE)) {
        const qsizetype len = qMin<qsizetype>(buffer->ulBufferLength, m_bufferBytes);
        storeCaptured(static_cast<const char *>(buffer->pBuffer), len);
        buffer->ulBufferLength = m_bufferBytes;
        m_mixSetup.pmixRead(m_mixSetup.ulMixHandle, buffer, 1);
    }
}

QList<QPair<unsigned short, QString>> QOS2DartStream::availableDevices()
{
    QList<QPair<unsigned short, QString>> devices;

    char buf[256];
    MCI_SYSINFO_PARMS sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));
    sysInfo.pszReturn = (PSZ)buf;
    sysInfo.ulRetSize = sizeof(buf);
    sysInfo.usDeviceType = MCI_DEVTYPE_AUDIO_AMPMIX;
    ULONG rc = mciSendCommand(0, MCI_SYSINFO, MCI_WAIT | MCI_SYSINFO_QUANTITY, &sysInfo, 0);
    if (mciFailed(rc)) {
        qCWarning(qLcOS2Dart) << "Unable to query audio devices:" << mciErrorString(rc);
        return devices;
    }
    const ULONG count = strtoul(buf, nullptr, 10);

    for (ULONG i = 1; i <= count; ++i) {
        QString description;

        memset(&sysInfo, 0, sizeof(sysInfo));
        sysInfo.pszReturn = (PSZ)buf;
        sysInfo.ulRetSize = sizeof(buf);
        sysInfo.ulNumber = i;
        sysInfo.usDeviceType = MCI_DEVTYPE_AUDIO_AMPMIX;
        rc = mciSendCommand(0, MCI_SYSINFO, MCI_WAIT | MCI_SYSINFO_INSTALLNAME, &sysInfo, 0);
        if (mciFailed(rc))
            continue;

        MCI_SYSINFO_LOGDEVICE logDevice;
        memset(&logDevice, 0, sizeof(logDevice));
        strncpy(logDevice.szInstallName, buf, sizeof(logDevice.szInstallName) - 1);
        description = QString::fromLocal8Bit(buf);

        sysInfo.ulItem = MCI_SYSINFO_QUERY_DRIVER;
        sysInfo.pSysInfoParm = &logDevice;
        rc = mciSendCommand(0, MCI_SYSINFO, MCI_WAIT | MCI_SYSINFO_ITEM, &sysInfo, 0);
        if (!mciFailed(rc) && logDevice.szProductInfo[0])
            description = QString::fromLocal8Bit(logDevice.szProductInfo).trimmed();

        devices.append({ static_cast<unsigned short>(i), description });
    }
    return devices;
}

QT_END_NAMESPACE
