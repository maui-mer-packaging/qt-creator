/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "defaultpropertyprovider.h"
#include "qbsconstants.h"

#include <projectexplorer/kit.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/toolchain.h>
#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>
#include <utils/qtcassert.h>

#include <tools/hostosinfo.h>

#include <QDir>
#include <QFileInfo>

namespace QbsProjectManager {
using namespace Constants;

static QString extractToolchainPrefix(QString *compilerName)
{
    QString prefix;
    if (compilerName->endsWith(QLatin1String("-g++"))
            || compilerName->endsWith(QLatin1String("-clang++"))) {
        const int idx = compilerName->lastIndexOf(QLatin1Char('-')) + 1;
        prefix = compilerName->left(idx);
        compilerName->remove(0, idx);
    }
    return prefix;
}

QVariantMap DefaultPropertyProvider::properties(const ProjectExplorer::Kit *k, const QVariantMap &defaultData) const
{
    QTC_ASSERT(k, return defaultData);
    QVariantMap data = defaultData;

    const QString sysroot = ProjectExplorer::SysRootKitInformation::sysRoot(k).toUserOutput();
    if (ProjectExplorer::SysRootKitInformation::hasSysRoot(k))
        data.insert(QLatin1String(QBS_SYSROOT), sysroot);

    ProjectExplorer::ToolChain *tc = ProjectExplorer::ToolChainKitInformation::toolChain(k);
    if (!tc)
        return data;

    // FIXME/CLARIFY: How to pass the sysroot?
    ProjectExplorer::Abi targetAbi = tc->targetAbi();
    if (targetAbi.architecture() != ProjectExplorer::Abi::UnknownArchitecture) {
        QString architecture = ProjectExplorer::Abi::toString(targetAbi.architecture());

        // We have to be conservative tacking on suffixes to arch names because an arch that is
        // already 64-bit may get an incorrect name as a result (i.e. Itanium)
        if (targetAbi.wordWidth() == 64) {
            switch (targetAbi.architecture()) {
            case ProjectExplorer::Abi::X86Architecture:
                architecture.append(QLatin1Char('_'));
                // fall through
            case ProjectExplorer::Abi::ArmArchitecture:
            case ProjectExplorer::Abi::MipsArchitecture:
            case ProjectExplorer::Abi::PowerPCArchitecture:
                architecture.append(QString::number(targetAbi.wordWidth()));
                break;
            default:
                break;
            }
        }

        data.insert(QLatin1String(QBS_ARCHITECTURE),
                    qbs::Internal::HostOsInfo::canonicalArchitecture(architecture));
    }

    switch (targetAbi.os()) {
    case ProjectExplorer::Abi::WindowsOS:
        data.insert(QLatin1String(QBS_TARGETOS), QLatin1String("windows"));
        data.insert(QLatin1String(QBS_TOOLCHAIN),
                    targetAbi.osFlavor() == ProjectExplorer::Abi::WindowsMSysFlavor
                    ? QStringList() << QLatin1String("mingw") << QLatin1String("gcc")
                    : QStringList() << QLatin1String("msvc"));
        break;
    case ProjectExplorer::Abi::MacOS: {
        const char IOSQT[] = "Qt4ProjectManager.QtVersion.Ios"; // from Ios::Constants (include header?)
        const char IOS_SIMULATOR_TYPE[] = "Ios.Simulator.Type";

        const QtSupport::BaseQtVersion * const qt = QtSupport::QtKitInformation::qtVersion(k);
        QStringList targetOS;
        targetOS << QLatin1String("darwin") << QLatin1String("bsd4")
                 << QLatin1String("bsd") << QLatin1String("unix");
        if (qt && qt->type() == QLatin1String(IOSQT)) {
            targetOS.insert(0, QLatin1String("ios"));
            if (ProjectExplorer::DeviceTypeKitInformation::deviceTypeId(k) == IOS_SIMULATOR_TYPE)
                targetOS.insert(0, QLatin1String("ios-simulator"));
        } else {
            targetOS.insert(0, QLatin1String("osx"));
        }
        data.insert(QLatin1String(QBS_TARGETOS), targetOS);

        if (tc->type() != QLatin1String("clang")) {
            data.insert(QLatin1String(QBS_TOOLCHAIN), QLatin1String("gcc"));
        } else {
            data.insert(QLatin1String(QBS_TOOLCHAIN),
                        QStringList() << QLatin1String("clang")
                        << QLatin1String("llvm")
                        << QLatin1String("gcc"));
        }

        // Set Xcode SDK name and version - required by Qbs if a sysroot is present
        // Ideally this would be done in a better way...
        QRegExp re(QLatin1String("(MacOSX|iPhoneOS|iPhoneSimulator)([0-9]+\\.[0-9]+)\\.sdk"));
        if (re.exactMatch(QDir(sysroot).dirName())) {
            data.insert(QLatin1String(CPP_XCODESDKNAME), QString(re.cap(1).toLower() + re.cap(2)));
            data.insert(QLatin1String(CPP_XCODESDKVERSION), re.cap(2));
        }
        break;
    }
    case ProjectExplorer::Abi::LinuxOS:
        data.insert(QLatin1String(QBS_TARGETOS), QStringList() << QLatin1String("linux")
                    << QLatin1String("unix"));
        if (tc->type() != QLatin1String("clang")) {
            data.insert(QLatin1String(QBS_TOOLCHAIN), QLatin1String("gcc"));
        } else {
            data.insert(QLatin1String(QBS_TOOLCHAIN),
                        QStringList() << QLatin1String("clang")
                        << QLatin1String("llvm")
                        << QLatin1String("gcc"));
        }
        break;
    default:
        // TODO: Factor out toolchain type setting.
        data.insert(QLatin1String(QBS_TARGETOS), QStringList() << QLatin1String("unix"));
        if (tc->type() != QLatin1String("clang")) {
            data.insert(QLatin1String(QBS_TOOLCHAIN), QLatin1String("gcc"));
        } else {
            data.insert(QLatin1String(QBS_TOOLCHAIN),
                        QStringList() << QLatin1String("clang")
                        << QLatin1String("llvm")
                        << QLatin1String("gcc"));
        }
    }

    Utils::FileName cxx = tc->compilerCommand();
    const QFileInfo cxxFileInfo = cxx.toFileInfo();
    QString compilerName = cxxFileInfo.fileName();
    const QString toolchainPrefix = extractToolchainPrefix(&compilerName);
    if (!toolchainPrefix.isEmpty())
        data.insert(QLatin1String(CPP_TOOLCHAINPREFIX), toolchainPrefix);
    data.insert(QLatin1String(CPP_COMPILERNAME), compilerName);
    if (targetAbi.os() != ProjectExplorer::Abi::WindowsOS
            || targetAbi.osFlavor() == ProjectExplorer::Abi::WindowsMSysFlavor) {
        data.insert(QLatin1String(CPP_LINKERNAME), compilerName);
    }
    data.insert(QLatin1String(CPP_TOOLCHAINPATH), cxxFileInfo.absolutePath());
    if (targetAbi.osFlavor() == ProjectExplorer::Abi::WindowsMsvc2013Flavor) {
        const QLatin1String flags("/FS");
        data.insert(QLatin1String(CPP_PLATFORMCFLAGS), flags);
        data.insert(QLatin1String(CPP_PLATFORMCXXFLAGS), flags);
    }
    return data;
}

} // namespace QbsProjectManager
