// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qpipewirecapture_p.h"
#include "qpipewirecapturehelper_p.h"

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

QPipeWireCapture::QPipeWireCapture(Source initialSource)
    : QPlatformSurfaceCapture(initialSource)
{
    m_helper = std::make_unique<QPipeWireCaptureHelper>(*this);
}

QPipeWireCapture::~QPipeWireCapture()
{
}

QVideoFrameFormat QPipeWireCapture::frameFormat() const
{
    if (m_helper)
        return m_helper->frameFormat();

    return QVideoFrameFormat();
}

bool QPipeWireCapture::setActiveInternal(bool active)
{
    if (!m_helper)
        m_helper = std::make_unique<QPipeWireCaptureHelper>(*this);

    if (m_helper)
        return m_helper->setActiveInternal(active);

    return static_cast<bool>(m_helper) == active;
}

QT_END_NAMESPACE
