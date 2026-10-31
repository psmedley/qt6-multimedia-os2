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
