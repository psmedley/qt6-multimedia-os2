// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "qfileutil_p.h"
#include <QtCore/qfile.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

bool copyAllFiles(const QDir &source, const QDir &dest)
{
    using namespace Qt::Literals;

    if (!source.exists() || !dest.exists())
        return false;

    QDirIterator it(source);
    bool success = true;
    while (it.hasNext()) {
        QFileInfo file{ it.next() };
        if (file.isFile()) {
            const QString destination = dest.absolutePath() + u"/"_s + file.fileName();

            if (QFile::exists(destination))
                if (!QFile::remove(destination))
                    success = false;

            if (!QFile::copy(file.absoluteFilePath(), destination))
                success = false;
        }
    }

    return success;
}

QT_END_NAMESPACE
