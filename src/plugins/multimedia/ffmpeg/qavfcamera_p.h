// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QAVFCAMERA_H
#define QAVFCAMERA_H

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

#include "qavfcamerabase_p.h"
#include <private/qplatformmediaintegration_p.h>
#include <private/qvideooutputorientationhandler_p.h>
#define AVMediaType XAVMediaType
#include "qffmpeghwaccel_p.h"
#undef AVMediaType

#include <qfilesystemwatcher.h>
#include <qsocketnotifier.h>
#include <qmutex.h>

#include <dispatch/dispatch.h>

Q_FORWARD_DECLARE_OBJC_CLASS(AVCaptureSession);
Q_FORWARD_DECLARE_OBJC_CLASS(AVCaptureDeviceInput);
Q_FORWARD_DECLARE_OBJC_CLASS(AVCaptureVideoDataOutput);
Q_FORWARD_DECLARE_OBJC_CLASS(AVCaptureDevice);
Q_FORWARD_DECLARE_OBJC_CLASS(QAVFSampleBufferDelegate);
Q_FORWARD_DECLARE_OBJC_CLASS(AVCaptureDeviceRotationCoordinator);

QT_BEGIN_NAMESPACE

class QFFmpegVideoSink;
struct VideoTransformation;

class QAVFCamera : public QAVFCameraBase
{
    Q_OBJECT

public:
    explicit QAVFCamera(QCamera *parent);
    ~QAVFCamera();

    void setCaptureSession(QPlatformMediaCaptureSession *) override;

    bool setCameraFormat(const QCameraFormat &format) override;

    std::optional<int> ffmpegHWPixelFormat() const override;

    int cameraPixelFormatScore(QVideoFrameFormat::PixelFormat pixelFmt,
                               QVideoFrameFormat::ColorRange colorRange) const override;

    QVideoFrameFormat frameFormat() const override;

protected:
    void onActiveChanged(bool active) override;
    void onCameraDeviceChanged(const QCameraDevice &device) override;

private:
    void updateCameraFormat();
    void updateVideoInput();
    void attachVideoInputDevice();
    void setPixelFormat(QVideoFrameFormat::PixelFormat pixelFormat, uint32_t inputCvPixFormat);
    QSize adjustedResolution() const;
    VideoTransformation surfaceTransform() const;

    void updateRotationTracking();
    int getCurrentRotationAngleDegrees() const;

    bool isFrontCamera() const;

    AVCaptureDevice *device() const;

    QMediaCaptureSession *m_session = nullptr;
    AVCaptureSession *m_captureSession = nullptr;
    AVCaptureDeviceInput *m_videoInput = nullptr;
    AVCaptureVideoDataOutput *m_videoDataOutput = nullptr;
    QAVFSampleBufferDelegate *m_sampleBufferDelegate = nullptr;
    dispatch_queue_t m_delegateQueue;
    AVPixelFormat m_hwPixelFormat = AV_PIX_FMT_NONE;
    uint32_t m_cvPixelFormat = 0;

    // If running iOS 17+, we use AVCaptureDeviceRotationCoordinator
    // to get the camera rotation directly from the camera-device.
    //
    // Gives us rotational information about the camera-device.
    AVCaptureDeviceRotationCoordinator *m_rotationCoordinator = nullptr;
#ifdef Q_OS_IOS
    // If running iOS 16 or older, we use the UIDeviceOrientation
    // and the AVCaptureCameraPosition to apply rotation metadata
    // to the cameras frames.
    bool m_receivingUiDeviceOrientationNotifications = false;
#endif
};

QT_END_NAMESPACE


#endif  // QFFMPEGCAMERA_H

