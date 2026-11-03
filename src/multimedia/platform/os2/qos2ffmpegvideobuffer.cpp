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


#include "qos2ffmpegvideobuffer_p.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

QT_BEGIN_NAMESPACE

QOS2FFmpegVideoBuffer::QOS2FFmpegVideoBuffer(AVFrame *frame)
    : QAbstractVideoBuffer(QVideoFrame::NoHandle),
      m_frame(frame)
{
}

QOS2FFmpegVideoBuffer::~QOS2FFmpegVideoBuffer()
{
    av_frame_free(&m_frame);
}

QVideoFrameFormat::PixelFormat QOS2FFmpegVideoBuffer::pixelFormat(int avPixelFormat)
{
    switch (avPixelFormat) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return QVideoFrameFormat::Format_YUV420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
        return QVideoFrameFormat::Format_YUV422P;
    case AV_PIX_FMT_NV12:
        return QVideoFrameFormat::Format_NV12;
    case AV_PIX_FMT_BGRA:
        return QVideoFrameFormat::Format_BGRA8888;
    case AV_PIX_FMT_BGR0:
        return QVideoFrameFormat::Format_BGRX8888;
    case AV_PIX_FMT_RGBA:
        return QVideoFrameFormat::Format_RGBA8888;
    case AV_PIX_FMT_RGB0:
        return QVideoFrameFormat::Format_RGBX8888;
    default:
        return QVideoFrameFormat::Format_Invalid;
    }
}

QVideoFrameFormat QOS2FFmpegVideoBuffer::format() const
{
    QVideoFrameFormat format(QSize(m_frame->width, m_frame->height), pixelFormat(m_frame->format));

    QVideoFrameFormat::YCbCrColorSpace colorSpace = QVideoFrameFormat::YCbCr_BT601;
    if (m_frame->color_range == AVCOL_RANGE_JPEG || m_frame->format == AV_PIX_FMT_YUVJ420P
        || m_frame->format == AV_PIX_FMT_YUVJ422P) {
        colorSpace = QVideoFrameFormat::YCbCr_JPEG;
    } else if (m_frame->colorspace == AVCOL_SPC_BT709) {
        colorSpace = QVideoFrameFormat::YCbCr_BT709;
    } else if (m_frame->colorspace == AVCOL_SPC_BT2020_NCL
               || m_frame->colorspace == AVCOL_SPC_BT2020_CL) {
        colorSpace = QVideoFrameFormat::YCbCr_BT2020;
    }
    format.setYCbCrColorSpace(colorSpace);
    return format;
}

QAbstractVideoBuffer::MapData QOS2FFmpegVideoBuffer::map(QVideoFrame::MapMode mode)
{
    MapData mapData;
    if (!m_frame || m_mapMode != QVideoFrame::NotMapped)
        return mapData;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(AVPixelFormat(m_frame->format));
    if (!desc)
        return mapData;

    const int planes = qMin(av_pix_fmt_count_planes(AVPixelFormat(m_frame->format)), 4);
    for (int i = 0; i < planes; ++i) {
        // Chroma planes of planar YUV are subsampled vertically
        const bool chroma = (i == 1 || i == 2) && !(desc->flags & AV_PIX_FMT_FLAG_RGB);
        const int height = chroma ? AV_CEIL_RSHIFT(m_frame->height, desc->log2_chroma_h)
                                  : m_frame->height;
        mapData.data[i] = m_frame->data[i];
        mapData.bytesPerLine[i] = m_frame->linesize[i];
        mapData.size[i] = m_frame->linesize[i] * height;
    }
    mapData.nPlanes = planes;
    m_mapMode = mode;
    return mapData;
}

void QOS2FFmpegVideoBuffer::unmap()
{
    m_mapMode = QVideoFrame::NotMapped;
}

QT_END_NAMESPACE
