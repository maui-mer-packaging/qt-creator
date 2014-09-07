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

#include "qbsproject.h"

#include "qbsbuildconfiguration.h"
#include "qbslogsink.h"
#include "qbsprojectfile.h"
#include "qbsprojectparser.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsnodes.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/icontext.h>
#include <coreplugin/id.h>
#include <coreplugin/icore.h>
#include <coreplugin/iversioncontrol.h>
#include <coreplugin/vcsmanager.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/mimedatabase.h>
#include <cpptools/cppmodelmanagerinterface.h>
#include <projectexplorer/buildenvironmentwidget.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/deploymentdata.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmacroexpander.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/headerpath.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/uicodemodelsupport.h>
#include <qmljstools/qmljsmodelmanager.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <qbs.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QVariantMap>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace QbsProjectManager {
namespace Internal {

// --------------------------------------------------------------------
// Constants:
// --------------------------------------------------------------------

static const char CONFIG_CPP_MODULE[] = "cpp";
static const char CONFIG_CXXFLAGS[] = "cxxFlags";
static const char CONFIG_CFLAGS[] = "cFlags";
static const char CONFIG_DEFINES[] = "defines";
static const char CONFIG_INCLUDEPATHS[] = "includePaths";
static const char CONFIG_SYSTEM_INCLUDEPATHS[] = "systemIncludePaths";
static const char CONFIG_FRAMEWORKPATHS[] = "frameworkPaths";
static const char CONFIG_SYSTEM_FRAMEWORKPATHS[] = "systemFrameworkPaths";
static const char CONFIG_PRECOMPILEDHEADER[] = "precompiledHeader";

// --------------------------------------------------------------------
// QbsProject:
// --------------------------------------------------------------------

QbsProject::QbsProject(QbsManager *manager, const QString &fileName) :
    m_manager(manager),
    m_projectName(QFileInfo(fileName).completeBaseName()),
    m_fileName(fileName),
    m_rootProjectNode(0),
    m_qbsProjectParser(0),
    m_qbsUpdateFutureInterface(0),
    m_parsingScheduled(false),
    m_cancelStatus(CancelStatusNone),
    m_currentBc(0)
{
    m_parsingDelay.setInterval(1000); // delay parsing by 1s.

    setId(Constants::PROJECT_ID);
    setProjectContext(Context(Constants::PROJECT_ID));
    setProjectLanguages(Context(ProjectExplorer::Constants::LANG_CXX));

    connect(this, SIGNAL(activeTargetChanged(ProjectExplorer::Target*)),
            this, SLOT(changeActiveTarget(ProjectExplorer::Target*)));
    connect(this, SIGNAL(addedTarget(ProjectExplorer::Target*)),
            this, SLOT(targetWasAdded(ProjectExplorer::Target*)));
    connect(this, SIGNAL(environmentChanged()), this, SLOT(delayParsing()));

    connect(&m_parsingDelay, SIGNAL(timeout()), this, SLOT(startParsing()));

    updateDocuments(QSet<QString>() << fileName);

    // NOTE: QbsProjectNode does not use this as a parent!
    m_rootProjectNode = new QbsRootProjectNode(this); // needs documents to be initialized!
}

QbsProject::~QbsProject()
{
    m_codeModelFuture.cancel();
    delete m_qbsProjectParser;
    if (m_qbsUpdateFutureInterface) {
        m_qbsUpdateFutureInterface->reportCanceled();
        delete m_qbsUpdateFutureInterface;
        m_qbsUpdateFutureInterface = 0;
    }

    // Deleting the root node triggers a few things, make sure rootProjectNode
    // returns 0 already
    QbsProjectNode *root = m_rootProjectNode;
    m_rootProjectNode = 0;
    delete root;
}

QString QbsProject::displayName() const
{
    return m_projectName;
}

IDocument *QbsProject::document() const
{
    foreach (IDocument *doc, m_qbsDocuments) {
        if (doc->filePath() == m_fileName)
            return doc;
    }
    QTC_ASSERT(false, return 0);
}

QbsManager *QbsProject::projectManager() const
{
    return m_manager;
}

ProjectNode *QbsProject::rootProjectNode() const
{
    return m_rootProjectNode;
}

static void collectFilesForProject(const qbs::ProjectData &project, QSet<QString> &result)
{
    result.insert(project.location().fileName());
    foreach (const qbs::ProductData &prd, project.products()) {
        foreach (const qbs::GroupData &grp, prd.groups()) {
            foreach (const QString &file, grp.allFilePaths())
                result.insert(file);
            result.insert(grp.location().fileName());
        }
        result.insert(prd.location().fileName());
    }
    foreach (const qbs::ProjectData &subProject, project.subProjects())
        collectFilesForProject(subProject, result);
}

QStringList QbsProject::files(Project::FilesMode fileMode) const
{
    Q_UNUSED(fileMode);
    if (!m_rootProjectNode || !m_rootProjectNode->qbsProjectData().isValid())
        return QStringList();
    QSet<QString> result;
    collectFilesForProject(m_rootProjectNode->qbsProjectData(), result);
    result.unite(qbsProject().buildSystemFiles());
    return result.toList();
}

bool QbsProject::isProjectEditable() const
{
    return m_qbsProject.isValid() && !isParsing() && !ProjectExplorer::BuildManager::isBuilding();
}

class ChangeExpector
{
public:
    ChangeExpector(const QString &filePath, const QSet<Core::IDocument *> &documents)
        : m_document(0)
    {
        foreach (Core::IDocument * const doc, documents) {
            if (doc->filePath() == filePath) {
                m_document = doc;
                break;
            }
        }
        QTC_ASSERT(m_document, return);
        Core::DocumentManager::expectFileChange(filePath);
        m_wasInDocumentManager = Core::DocumentManager::removeDocument(m_document);
        QTC_CHECK(m_wasInDocumentManager);
    }

    ~ChangeExpector()
    {
        QTC_ASSERT(m_document, return);
        Core::DocumentManager::addDocument(m_document);
        Core::DocumentManager::unexpectFileChange(m_document->filePath());
    }

private:
    Core::IDocument *m_document;
    bool m_wasInDocumentManager;
};

bool QbsProject::ensureWriteableQbsFile(const QString &file)
{
    // Ensure that the file is not read only
    QFileInfo fi(file);
    if (!fi.isWritable()) {
        // Try via vcs manager
        Core::IVersionControl *versionControl =
            Core::VcsManager::findVersionControlForDirectory(fi.absolutePath());
        if (!versionControl || !versionControl->vcsOpen(file)) {
            bool makeWritable = QFile::setPermissions(file, fi.permissions() | QFile::WriteUser);
            if (!makeWritable) {
                QMessageBox::warning(Core::ICore::mainWindow(),
                                     tr("Failed!"),
                                     tr("Could not write project file %1.").arg(file));
                return false;
            }
        }
    }
    return true;
}

bool QbsProject::addFilesToProduct(QbsBaseProjectNode *node, const QStringList &filePaths,
        const qbs::ProductData &productData, const qbs::GroupData &groupData, QStringList *notAdded)
{
    QTC_ASSERT(m_qbsProject.isValid(), return false);
    QStringList allPaths = groupData.allFilePaths();
    const QString productFilePath = productData.location().fileName();
    ChangeExpector expector(productFilePath, m_qbsDocuments);
    ensureWriteableQbsFile(productFilePath);
    foreach (const QString &path, filePaths) {
        qbs::ErrorInfo err = m_qbsProject.addFiles(productData, groupData, QStringList() << path);
        if (err.hasError()) {
            Core::MessageManager::write(err.toString());
            *notAdded += path;
        } else {
            allPaths += path;
        }
    }
    if (notAdded->count() != filePaths.count()) {
        m_projectData = m_qbsProject.projectData();
        QbsGroupNode::setupFiles(node, allPaths, QFileInfo(productFilePath).absolutePath(), true);
        m_rootProjectNode->update();
    }
    return notAdded->isEmpty();
}

bool QbsProject::removeFilesFromProduct(QbsBaseProjectNode *node, const QStringList &filePaths,
        const qbs::ProductData &productData, const qbs::GroupData &groupData,
        QStringList *notRemoved)
{
    QTC_ASSERT(m_qbsProject.isValid(), return false);
    QStringList allPaths = groupData.allFilePaths();
    const QString productFilePath = productData.location().fileName();
    ChangeExpector expector(productFilePath, m_qbsDocuments);
    ensureWriteableQbsFile(productFilePath);
    foreach (const QString &path, filePaths) {
        qbs::ErrorInfo err
                = m_qbsProject.removeFiles(productData, groupData, QStringList() << path);
        if (err.hasError()) {
            Core::MessageManager::write(err.toString());
            *notRemoved += path;
        } else {
            allPaths.removeOne(path);
        }
    }
    if (notRemoved->count() != filePaths.count()) {
        m_projectData = m_qbsProject.projectData();
        QbsGroupNode::setupFiles(node, allPaths, QFileInfo(productFilePath).absolutePath(), true);
        m_rootProjectNode->update();
    }
    return notRemoved->isEmpty();
}

void QbsProject::invalidate()
{
    prepareForParsing();
}

qbs::BuildJob *QbsProject::build(const qbs::BuildOptions &opts, QStringList productNames)
{
    if (!qbsProject().isValid() || isParsing())
        return 0;
    if (productNames.isEmpty()) {
        return qbsProject().buildAllProducts(opts);
    } else {
        QList<qbs::ProductData> products;
        foreach (const QString &productName, productNames) {
            bool found = false;
            foreach (const qbs::ProductData &data, qbsProjectData().allProducts()) {
                if (data.name() == productName) {
                    found = true;
                    products.append(data);
                    break;
                }
            }
            if (!found)
                return 0;
        }

        return qbsProject().buildSomeProducts(products, opts);
    }
}

qbs::CleanJob *QbsProject::clean(const qbs::CleanOptions &opts)
{
    if (!qbsProject().isValid())
        return 0;
    return qbsProject().cleanAllProducts(opts);
}

qbs::InstallJob *QbsProject::install(const qbs::InstallOptions &opts)
{
    if (!qbsProject().isValid())
        return 0;
    return qbsProject().installAllProducts(opts);
}

QString QbsProject::profileForTarget(const Target *t) const
{
    return m_manager->profileForKit(t->kit());
}

bool QbsProject::isParsing() const
{
    return m_qbsUpdateFutureInterface;
}

bool QbsProject::hasParseResult() const
{
    return qbsProject().isValid();
}

Utils::FileName QbsProject::defaultBuildDirectory(const QString &projectFilePath, const Kit *k,
                                                  const QString &bcName)
{
    const QString projectName = QFileInfo(projectFilePath).completeBaseName();
    ProjectExplorer::ProjectMacroExpander expander(projectFilePath, projectName, k, bcName);
    QString projectDir = projectDirectory(Utils::FileName::fromString(projectFilePath)).toString();
    QString buildPath = Utils::expandMacros(Core::DocumentManager::buildDirectory(), &expander);
    return Utils::FileName::fromString(Utils::FileUtils::resolvePath(projectDir, buildPath));
}

qbs::Project QbsProject::qbsProject() const
{
    return m_qbsProject;
}

qbs::ProjectData QbsProject::qbsProjectData() const
{
    return m_projectData;
}

bool QbsProject::needsSpecialDeployment() const
{
    return true;
}

void QbsProject::handleQbsParsingDone(bool success)
{
    QTC_ASSERT(m_qbsProjectParser, return);

    const CancelStatus cancelStatus = m_cancelStatus;
    m_cancelStatus = CancelStatusNone;

    // Start a new one parse operation right away, ignoring the old result.
    if (cancelStatus == CancelStatusCancelingForReparse) {
        m_qbsProjectParser->deleteLater();
        m_qbsProjectParser = 0;
        parseCurrentBuildConfiguration();
        return;
    }

    generateErrors(m_qbsProjectParser->error());

    if (success) {
        m_qbsProject = m_qbsProjectParser->qbsProject();
        const qbs::ProjectData &projectData = m_qbsProject.projectData();
        QTC_CHECK(m_qbsProject.isValid());
        if (projectData != m_projectData) {
            m_projectData = projectData;
            readQbsData();
        }
    } else {
        m_qbsUpdateFutureInterface->reportCanceled();
    }

    m_qbsProjectParser->deleteLater();
    m_qbsProjectParser = 0;

    if (m_qbsUpdateFutureInterface) {
        m_qbsUpdateFutureInterface->reportFinished();
        delete m_qbsUpdateFutureInterface;
        m_qbsUpdateFutureInterface = 0;
    }

    emit projectParsingDone(success);
}

void QbsProject::targetWasAdded(Target *t)
{
    connect(t, SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
            this, SLOT(delayParsing()));
    connect(t, SIGNAL(buildDirectoryChanged()), this, SLOT(delayParsing()));
}

void QbsProject::changeActiveTarget(Target *t)
{
    BuildConfiguration *bc = 0;
    if (t && t->kit())
        bc = t->activeBuildConfiguration();
    buildConfigurationChanged(bc);
}

void QbsProject::buildConfigurationChanged(BuildConfiguration *bc)
{
    if (m_currentBc)
        disconnect(m_currentBc, SIGNAL(qbsConfigurationChanged()), this, SLOT(delayParsing()));

    m_currentBc = qobject_cast<QbsBuildConfiguration *>(bc);
    if (m_currentBc) {
        connect(m_currentBc, SIGNAL(qbsConfigurationChanged()), this, SLOT(delayParsing()));
        delayParsing();
    } else {
        invalidate();
    }
}

void QbsProject::startParsing()
{
    // Qbs does update the build graph during the build. So we cannot
    // start to parse while a build is running or we will lose information.
    if (ProjectExplorer::BuildManager::isBuilding(this)) {
        scheduleParsing();
        return;
    }

    parseCurrentBuildConfiguration();
}

void QbsProject::delayParsing()
{
    m_parsingDelay.start();
}

// Qbs may change its data
void QbsProject::readQbsData()
{
    QTC_ASSERT(m_rootProjectNode, return);

    m_rootProjectNode->update();

    qbs::Project project = m_rootProjectNode->qbsProject();
    updateDocuments(project.isValid() ? project.buildSystemFiles() : QSet<QString>() << m_fileName);

    qbs::ProjectData data = m_rootProjectNode->qbsProjectData();
    updateCppCodeModel(data);
    updateQmlJsCodeModel(data);
    updateBuildTargetData();

    emit fileListChanged();
}

void QbsProject::parseCurrentBuildConfiguration()
{
    m_parsingScheduled = false;
    if (m_cancelStatus == CancelStatusCancelingForReparse)
        return;

    // The CancelStatusCancelingAltoghether type can only be set by a build job, during
    // which no other parse requests come through to this point (except by the build job itself,
    // but of course not while canceling is in progress).
    QTC_ASSERT(m_cancelStatus == CancelStatusNone, return);

    if (!activeTarget())
        return;
    QbsBuildConfiguration *bc = qobject_cast<QbsBuildConfiguration *>(activeTarget()->activeBuildConfiguration());
    if (!bc)
        return;

    // New parse requests override old ones.
    // NOTE: We need to wait for the current operation to finish, since otherwise there could
    //       be a conflict. Consider the case where the old qbs::ProjectSetupJob is writing
    //       to the build graph file when the cancel request comes in. If we don't wait for
    //       acknowledgment, it might still be doing that when the new one already reads from the
    //       same file.
    if (m_qbsProjectParser) {
        m_cancelStatus = CancelStatusCancelingForReparse;
        m_qbsProjectParser->cancel();
        return;
    }

    parse(bc->qbsConfiguration(), bc->environment(), bc->buildDirectory().toString());
}

void QbsProject::cancelParsing()
{
    QTC_ASSERT(m_qbsProjectParser, return);
    m_cancelStatus = CancelStatusCancelingAltoghether;
    m_qbsProjectParser->cancel();
}

void QbsProject::updateAfterBuild()
{
    QTC_ASSERT(m_qbsProject.isValid(), return);
    m_projectData = m_qbsProject.projectData();
    updateBuildTargetData();
}

void QbsProject::registerQbsProjectParser(QbsProjectParser *p)
{
    m_parsingDelay.stop();

    if (m_qbsProjectParser) {
        m_qbsProjectParser->disconnect(this);
        m_qbsProjectParser->deleteLater();
    }

    m_qbsProjectParser = p;

    if (p)
        connect(m_qbsProjectParser, SIGNAL(done(bool)), this, SLOT(handleQbsParsingDone(bool)));
}

bool QbsProject::fromMap(const QVariantMap &map)
{
    if (!Project::fromMap(map))
        return false;

    Kit *defaultKit = KitManager::defaultKit();
    if (!activeTarget() && defaultKit) {
        Target *t = new Target(this, defaultKit);
        t->updateDefaultBuildConfigurations();
        t->updateDefaultDeployConfigurations();
        t->updateDefaultRunConfigurations();
        addTarget(t);
    }

    return true;
}

void QbsProject::generateErrors(const qbs::ErrorInfo &e)
{
    foreach (const qbs::ErrorItem &item, e.items())
        TaskHub::addTask(Task::Error, item.description(),
                         ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM,
                         FileName::fromString(item.codeLocation().fileName()),
                         item.codeLocation().line());

}

void QbsProject::parse(const QVariantMap &config, const Environment &env, const QString &dir)
{
    prepareForParsing();
    QTC_ASSERT(!m_qbsProjectParser, return);

    registerQbsProjectParser(new QbsProjectParser(this, m_qbsUpdateFutureInterface));

    m_qbsProjectParser->parse(config, env, dir);
    emit projectParsingStarted();
}

void QbsProject::prepareForParsing()
{
    TaskHub::clearTasks(ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM);
    if (m_qbsUpdateFutureInterface) {
        m_qbsUpdateFutureInterface->reportCanceled();
        m_qbsUpdateFutureInterface->reportFinished();
    }
    delete m_qbsUpdateFutureInterface;
    m_qbsUpdateFutureInterface = 0;

    m_qbsUpdateFutureInterface = new QFutureInterface<bool>();
    m_qbsUpdateFutureInterface->setProgressRange(0, 0);
    ProgressManager::addTask(m_qbsUpdateFutureInterface->future(),
        tr("Reading Project \"%1\"").arg(displayName()), "Qbs.QbsEvaluate");
    m_qbsUpdateFutureInterface->reportStarted();
}

void QbsProject::updateDocuments(const QSet<QString> &files)
{
    // Update documents:
    QSet<QString> newFiles = files;
    QTC_ASSERT(!newFiles.isEmpty(), newFiles << m_fileName);
    QSet<QString> oldFiles;
    foreach (IDocument *doc, m_qbsDocuments)
        oldFiles.insert(doc->filePath());

    QSet<QString> filesToAdd = newFiles;
    filesToAdd.subtract(oldFiles);
    QSet<QString> filesToRemove = oldFiles;
    filesToRemove.subtract(newFiles);

    QSet<IDocument *> currentDocuments = m_qbsDocuments;
    foreach (IDocument *doc, currentDocuments) {
        if (filesToRemove.contains(doc->filePath())) {
            m_qbsDocuments.remove(doc);
            delete doc;
        }
    }
    QSet<IDocument *> toAdd;
    foreach (const QString &f, filesToAdd)
        toAdd.insert(new QbsProjectFile(this, f));

    DocumentManager::addDocuments(toAdd.toList());
    m_qbsDocuments.unite(toAdd);
}

void QbsProject::updateCppCodeModel(const qbs::ProjectData &prj)
{
    if (!prj.isValid())
        return;

    QtSupport::BaseQtVersion *qtVersion =
            QtSupport::QtKitInformation::qtVersion(activeTarget()->kit());

    CppTools::CppModelManagerInterface *modelmanager =
        CppTools::CppModelManagerInterface::instance();

    if (!modelmanager)
        return;

    CppTools::ProjectInfo pinfo = modelmanager->projectInfo(this);
    pinfo.clearProjectParts();

    CppTools::ProjectPartBuilder ppBuilder(pinfo);

    if (qtVersion) {
        if (qtVersion->qtVersion() < QtSupport::QtVersionNumber(5,0,0))
            ppBuilder.setQtVersion(CppTools::ProjectPart::Qt4);
        else
            ppBuilder.setQtVersion(CppTools::ProjectPart::Qt5);
    } else {
        ppBuilder.setQtVersion(CppTools::ProjectPart::NoQt);
    }

    QHash<QString, QString> uiFiles;
    foreach (const qbs::ProductData &prd, prj.allProducts()) {
        foreach (const qbs::GroupData &grp, prd.groups()) {
            const qbs::PropertyMap &props = grp.properties();

            ppBuilder.setCxxFlags(props.getModulePropertiesAsStringList(
                                      QLatin1String(CONFIG_CPP_MODULE),
                                      QLatin1String(CONFIG_CXXFLAGS)));
            ppBuilder.setCFlags(props.getModulePropertiesAsStringList(
                                    QLatin1String(CONFIG_CPP_MODULE),
                                    QLatin1String(CONFIG_CFLAGS)));

            QStringList list = props.getModulePropertiesAsStringList(
                        QLatin1String(CONFIG_CPP_MODULE),
                        QLatin1String(CONFIG_DEFINES));
            QByteArray grpDefines;
            foreach (const QString &def, list) {
                QByteArray data = def.toUtf8();
                int pos = data.indexOf('=');
                if (pos >= 0)
                    data[pos] = ' ';
                grpDefines += (QByteArray("#define ") + data + '\n');
            }
            ppBuilder.setDefines(grpDefines);

            list = props.getModulePropertiesAsStringList(QLatin1String(CONFIG_CPP_MODULE),
                                                         QLatin1String(CONFIG_INCLUDEPATHS));
            list.append(props.getModulePropertiesAsStringList(QLatin1String(CONFIG_CPP_MODULE),
                                                              QLatin1String(CONFIG_SYSTEM_INCLUDEPATHS)));
            CppTools::ProjectPart::HeaderPaths grpHeaderPaths;
            foreach (const QString &p, list)
                grpHeaderPaths += CppTools::ProjectPart::HeaderPath(
                            FileName::fromUserInput(p).toString(),
                            CppTools::ProjectPart::HeaderPath::IncludePath);

            list = props.getModulePropertiesAsStringList(QLatin1String(CONFIG_CPP_MODULE),
                                                         QLatin1String(CONFIG_FRAMEWORKPATHS));
            list.append(props.getModulePropertiesAsStringList(QLatin1String(CONFIG_CPP_MODULE),
                                                              QLatin1String(CONFIG_SYSTEM_FRAMEWORKPATHS)));
            foreach (const QString &p, list)
                grpHeaderPaths += CppTools::ProjectPart::HeaderPath(
                            FileName::fromUserInput(p).toString(),
                            CppTools::ProjectPart::HeaderPath::FrameworkPath);

            ppBuilder.setHeaderPaths(grpHeaderPaths);

            const QString pch = props.getModuleProperty(QLatin1String(CONFIG_CPP_MODULE),
                    QLatin1String(CONFIG_PRECOMPILEDHEADER)).toString();
            ppBuilder.setPreCompiledHeaders(QStringList() << pch);

            ppBuilder.setDisplayName(grp.name());
            ppBuilder.setProjectFile(QString::fromLatin1("%1:%2:%3")
                    .arg(grp.location().fileName())
                    .arg(grp.location().line())
                    .arg(grp.location().column()));

            foreach (const QString &file, grp.allFilePaths()) {
                if (file.endsWith(QLatin1String(".ui"))) {
                    QStringList generated = m_rootProjectNode->qbsProject()
                            .generatedFiles(prd, file, QStringList(QLatin1String("hpp")));
                    if (generated.count() == 1)
                        uiFiles.insert(file, generated.at(0));
                }
            }

            const QList<Core::Id> languages =
                    ppBuilder.createProjectPartsForFiles(grp.allFilePaths());
            foreach (Core::Id language, languages)
                setProjectLanguage(language, true);
        }
    }

    if (pinfo.projectParts().isEmpty())
        return;

    QtSupport::UiCodeModelManager::update(this, uiFiles);

    // Register update the code model:
    m_codeModelFuture = modelmanager->updateProjectInfo(pinfo);
}

void QbsProject::updateQmlJsCodeModel(const qbs::ProjectData &prj)
{
    Q_UNUSED(prj);
    QmlJS::ModelManagerInterface *modelManager = QmlJS::ModelManagerInterface::instance();
    if (!modelManager)
        return;

    QmlJS::ModelManagerInterface::ProjectInfo projectInfo =
            modelManager->defaultProjectInfoForProject(this);

    setProjectLanguage(ProjectExplorer::Constants::LANG_QMLJS, !projectInfo.sourceFiles.isEmpty());
    modelManager->updateProjectInfo(projectInfo, this);
}

void QbsProject::updateApplicationTargets(const qbs::ProjectData &projectData)
{
    ProjectExplorer::BuildTargetInfoList applications;
    foreach (const qbs::ProductData &productData, projectData.allProducts()) {
        if (!productData.isEnabled() || !productData.isRunnable())
            continue;
        if (productData.targetArtifacts().isEmpty()) { // No build yet.
            applications.list << ProjectExplorer::BuildTargetInfo(Utils::FileName(),
                    Utils::FileName::fromString(productData.location().fileName()));
            continue;
        }
        foreach (const qbs::TargetArtifact &ta, productData.targetArtifacts()) {
            QTC_ASSERT(ta.isValid(), continue);
            if (!ta.isExecutable())
                continue;
            applications.list << ProjectExplorer::BuildTargetInfo(Utils::FileName::fromString(ta.filePath()),
                    Utils::FileName::fromString(productData.location().fileName()));
        }
    }
    activeTarget()->setApplicationTargets(applications);
}

void QbsProject::updateDeploymentInfo(const qbs::Project &project)
{
    ProjectExplorer::DeploymentData deploymentData;
    if (project.isValid()) {
        qbs::InstallOptions installOptions;
        installOptions.setInstallRoot(QLatin1String("/"));
        foreach (const qbs::InstallableFile &f,
                 project.installableFilesForProject(m_projectData, installOptions)) {
            deploymentData.addFile(f.sourceFilePath(), f.targetDirectory(), f.isExecutable()
                                   ? ProjectExplorer::DeployableFile::TypeExecutable
                                   : ProjectExplorer::DeployableFile::TypeNormal);
        }
    }
    activeTarget()->setDeploymentData(deploymentData);
}

void QbsProject::updateBuildTargetData()
{
    updateApplicationTargets(m_projectData);
    updateDeploymentInfo(m_qbsProject);
    foreach (Target *t, targets())
        t->updateDefaultRunConfigurations();
}

} // namespace Internal
} // namespace QbsProjectManager
