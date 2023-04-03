// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qwasmmediacapturesession_p.h"
#include "mediacapture/qwasmimagecapture_p.h"

#include "qwasmcamera_p.h"

Q_LOGGING_CATEGORY(qWasmMediaCaptureSession, "qt.multimedia.wasm.capturesession")

QWasmMediaCaptureSession::QWasmMediaCaptureSession() = default;

QWasmMediaCaptureSession::~QWasmMediaCaptureSession() = default;

QPlatformCamera *QWasmMediaCaptureSession::camera()
{
    return m_camera.data();
}

void QWasmMediaCaptureSession::setCamera(QPlatformCamera *camera)
{
    if (!camera)
        return;
    QWasmCamera *wasmCamera = static_cast<QWasmCamera *>(camera);
    if (!wasmCamera || m_camera.data() == wasmCamera)
        return;
    m_camera.reset(wasmCamera);
    emit cameraChanged();
    m_camera->setCaptureSession(this);
}

QPlatformImageCapture *QWasmMediaCaptureSession::imageCapture()
{
    return m_imageCapture;
}

void QWasmMediaCaptureSession::setImageCapture(QPlatformImageCapture *imageCapture)
{
    if (m_imageCapture == imageCapture)
        return;

    if (m_imageCapture)
        m_imageCapture->setCaptureSession(nullptr);

    m_imageCapture = static_cast<QWasmImageCapture *>(imageCapture);

    if (m_imageCapture) {
        m_imageCapture->setCaptureSession(this);

        m_imageCapture->setReadyForCapture(true);
        emit imageCaptureChanged();
    }
}

QPlatformMediaRecorder *QWasmMediaCaptureSession::mediaRecorder()
{
    return m_mediaRecorder;
}

void QWasmMediaCaptureSession::setMediaRecorder(QPlatformMediaRecorder *mediaRecorder)
{
    if (m_mediaRecorder == mediaRecorder)
        return;

    if (m_mediaRecorder)
        m_mediaRecorder->setCaptureSession(nullptr);

    m_mediaRecorder = static_cast<QWasmMediaRecorder *>(mediaRecorder);

    if (m_mediaRecorder)
        m_mediaRecorder->setCaptureSession(this);
}

void QWasmMediaCaptureSession::setAudioInput(QPlatformAudioInput *input)
{
    if (m_audioInput == input)
        return;

    m_needsAudio = !input;
    m_audioInput = input;
}

void QWasmMediaCaptureSession::setVideoPreview(QVideoSink *sink)
{
    if (m_wasmSink == sink)
        return;
    m_wasmSink = sink;
}

void QWasmMediaCaptureSession::setAudioOutput(QPlatformAudioOutput *output)
{
    if (m_audioOutput == output)
        return;
    m_audioOutput = output;
}
