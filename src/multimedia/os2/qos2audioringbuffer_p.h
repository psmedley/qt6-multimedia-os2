// Copyright (C) 2026 Paul Smedley
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QOS2AUDIORINGBUFFER_P_H
#define QOS2AUDIORINGBUFFER_P_H

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

#include <QtCore/qbytearray.h>
#include <QtCore/qmutex.h>

#include <string.h>

QT_BEGIN_NAMESPACE

// A simple thread safe byte FIFO used to hand audio data between the
// Qt (GUI/event loop) thread and the DART callback thread.
class QOS2AudioRingBuffer
{
public:
    void resize(qsizetype size)
    {
        QMutexLocker locker(&m_mutex);
        m_buffer.resize(qMax<qsizetype>(size, 0));
        m_readPos = 0;
        m_used = 0;
    }

    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_readPos = 0;
        m_used = 0;
    }

    qsizetype capacity() const
    {
        QMutexLocker locker(&m_mutex);
        return m_buffer.size();
    }

    qsizetype used() const
    {
        QMutexLocker locker(&m_mutex);
        return m_used;
    }

    qsizetype free() const
    {
        QMutexLocker locker(&m_mutex);
        return m_buffer.size() - m_used;
    }

    qsizetype write(const char *data, qsizetype len)
    {
        QMutexLocker locker(&m_mutex);
        const qsizetype cap = m_buffer.size();
        len = qMin(len, cap - m_used);
        if (len <= 0)
            return 0;
        qsizetype writePos = (m_readPos + m_used) % cap;
        const qsizetype first = qMin(len, cap - writePos);
        memcpy(m_buffer.data() + writePos, data, first);
        if (len > first)
            memcpy(m_buffer.data(), data + first, len - first);
        m_used += len;
        return len;
    }

    qsizetype read(char *data, qsizetype len)
    {
        QMutexLocker locker(&m_mutex);
        const qsizetype cap = m_buffer.size();
        len = qMin(len, m_used);
        if (len <= 0)
            return 0;
        const qsizetype first = qMin(len, cap - m_readPos);
        memcpy(data, m_buffer.constData() + m_readPos, first);
        if (len > first)
            memcpy(data + first, m_buffer.constData(), len - first);
        m_readPos = (m_readPos + len) % cap;
        m_used -= len;
        return len;
    }

private:
    mutable QMutex m_mutex;
    QByteArray m_buffer;
    qsizetype m_readPos = 0;
    qsizetype m_used = 0;
};

QT_END_NAMESPACE

#endif // QOS2AUDIORINGBUFFER_P_H
