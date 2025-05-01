// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#ifndef QAUTORESETEVENT_LINUX_P_H
#define QAUTORESETEVENT_LINUX_P_H

#include <QtCore/qsocketnotifier.h>
#include <QtMultimedia/qtmultimediaexports.h>

QT_BEGIN_NAMESPACE

namespace QtPrivate {

class Q_MULTIMEDIA_EXPORT QAutoResetEventEventFD : public QObject
{
    Q_OBJECT

public:
    explicit QAutoResetEventEventFD(QObject *parent = nullptr);

    ~QAutoResetEventEventFD();

    bool isValid() const { return m_fd != -1; }
    void set();

Q_SIGNALS:
    void activated();

private:
    QSocketNotifier m_notifier;
    int m_fd = -1;
};

} // namespace QtPrivate

QT_END_NAMESPACE

#endif // QAUTORESETEVENT_LINUX_P_H
