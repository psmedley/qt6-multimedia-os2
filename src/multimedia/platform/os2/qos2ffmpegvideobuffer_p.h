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


#ifndef QOS2FFMPEGVIDEOBUFFER_P_H
#define QOS2FFMPEGVIDEOBUFFER_P_H

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
#include <private/qabstractvideobuffer_p.h>
#include <QtMultimedia/qvideoframeformat.h>

extern "C" {
struct AVFrame;
}

QT_BEGIN_NAMESPACE

// Exposes the planes of a decoded AVFrame to QVideoFrame without copying.
class QOS2FFmpegVideoBuffer : public QAbstractVideoBuffer
{
public:
    // Takes ownership of frame
    explicit QOS2FFmpegVideoBuffer(AVFrame *frame);
    ~QOS2FFmpegVideoBuffer() override;

    QVideoFrame::MapMode mapMode() const override { return m_mapMode; }
    MapData map(QVideoFrame::MapMode mode) override;
    void unmap() override;

    QVideoFrameFormat format() const;

    static QVideoFrameFormat::PixelFormat pixelFormat(int avPixelFormat);

private:
    AVFrame *m_frame = nullptr;
    QVideoFrame::MapMode m_mapMode = QVideoFrame::NotMapped;
};

QT_END_NAMESPACE

#endif // QOS2FFMPEGVIDEOBUFFER_P_H
