// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QAUDIO_RTSAN_SUPPORT_P_H
#define QAUDIO_RTSAN_SUPPORT_P_H

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

// rtsan
#if defined(__has_cpp_attribute) && __has_cpp_attribute(clang::nonblocking)
#  define QT_MM_NONBLOCKING [[clang::nonblocking]]
#else
#  define QT_MM_NONBLOCKING
#endif

#endif // QAUDIO_RTSAN_SUPPORT_P_H
