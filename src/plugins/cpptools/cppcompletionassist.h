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

#ifndef CPPCOMPLETIONASSIST_H
#define CPPCOMPLETIONASSIST_H

#include "cppcompletionassistprovider.h"
#include "cppmodelmanagerinterface.h"
#include "cppworkingcopy.h"

#include <cplusplus/Icons.h>
#include <cplusplus/Symbol.h>
#include <cplusplus/TypeOfExpression.h>

#include <texteditor/basetexteditor.h>
#include <texteditor/codeassist/basicproposalitemlistmodel.h>
#include <texteditor/codeassist/defaultassistinterface.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/snippets/snippetassistcollector.h>

#include <utils/qtcoverride.h>

#include <QStringList>
#include <QVariant>

QT_BEGIN_NAMESPACE
class QTextCursor;
QT_END_NAMESPACE

namespace CPlusPlus {
class LookupItem;
class ClassOrNamespace;
class Function;
class LookupContext;
} // namespace CPlusPlus

namespace CppTools {
namespace Internal {

class CppCompletionAssistInterface;

class CppAssistProposalModel : public TextEditor::BasicProposalItemListModel
{
public:
    CppAssistProposalModel()
        : TextEditor::BasicProposalItemListModel()
        , m_completionOperator(CPlusPlus::T_EOF_SYMBOL)
        , m_replaceDotForArrow(false)
        , m_typeOfExpression(new CPlusPlus::TypeOfExpression)
    {
        m_typeOfExpression->setExpandTemplates(true);
    }

    bool isSortable(const QString &prefix) const QTC_OVERRIDE;
    TextEditor::IAssistProposalItem *proposalItem(int index) const QTC_OVERRIDE;

    unsigned m_completionOperator;
    bool m_replaceDotForArrow;
    QSharedPointer<CPlusPlus::TypeOfExpression> m_typeOfExpression;
};

class InternalCompletionAssistProvider : public CppCompletionAssistProvider
{
    Q_OBJECT

public:
    TextEditor::IAssistProcessor *createProcessor() const QTC_OVERRIDE;

    TextEditor::IAssistInterface *createAssistInterface(
            const QString &filePath,
            QTextDocument *document,
            bool isObjCEnabled,
            int position,
            TextEditor::AssistReason reason) const QTC_OVERRIDE;
};

class CppCompletionAssistProcessor : public TextEditor::IAssistProcessor
{
public:
    CppCompletionAssistProcessor();
    ~CppCompletionAssistProcessor();

    TextEditor::IAssistProposal *perform(const TextEditor::IAssistInterface *interface) QTC_OVERRIDE;

private:
    TextEditor::IAssistProposal *createContentProposal();
    TextEditor::IAssistProposal *createHintProposal(QList<CPlusPlus::Function *> symbols) const;
    bool accepts() const;

    int startOfOperator(int pos, unsigned *kind, bool wantFunctionCall) const;
    int findStartOfName(int pos = -1) const;
    int startCompletionHelper();
    bool tryObjCCompletion();
    bool objcKeywordsWanted() const;
    int startCompletionInternal(const QString &fileName,
                                unsigned line, unsigned column,
                                const QString &expression,
                                int endOfExpression);

    void completeObjCMsgSend(CPlusPlus::ClassOrNamespace *binding, bool staticClassAccess);
    bool completeInclude(const QTextCursor &cursor);
    void completeInclude(const QString &realPath, const QStringList &suffixes);
    void completePreprocessor();
    bool completeConstructorOrFunction(const QList<CPlusPlus::LookupItem> &results,
                                       int endOfExpression,
                                       bool toolTipOnly);
    bool completeMember(const QList<CPlusPlus::LookupItem> &results);
    bool completeScope(const QList<CPlusPlus::LookupItem> &results);
    void completeNamespace(CPlusPlus::ClassOrNamespace *binding);
    void completeClass(CPlusPlus::ClassOrNamespace *b, bool staticLookup = true);
    void addClassMembersToCompletion(CPlusPlus::Scope *scope, bool staticLookup);
    bool completeQtMethod(const QList<CPlusPlus::LookupItem> &results, bool wantSignals);
    bool completeSignal(const QList<CPlusPlus::LookupItem> &results)
    { return completeQtMethod(results, true); }
    bool completeSlot(const QList<CPlusPlus::LookupItem> &results)
    { return completeQtMethod(results, false); }
    void globalCompletion(CPlusPlus::Scope *scope);

    void addCompletionItem(const QString &text,
                           const QIcon &icon = QIcon(),
                           int order = 0,
                           const QVariant &data = QVariant());
    void addCompletionItem(CPlusPlus::Symbol *symbol,
                           int order = 0);
    void addSnippets();
    void addKeywords();
    void addMacros(const QString &fileName, const CPlusPlus::Snapshot &snapshot);
    void addMacros_helper(const CPlusPlus::Snapshot &snapshot,
                          const QString &fileName,
                          QSet<QString> *processed,
                          QSet<QString> *definedMacros);

    int m_startPosition;
    CPlusPlus::LanguageFeatures m_languageFeatures;
    QScopedPointer<const CppCompletionAssistInterface> m_interface;
    QList<TextEditor::BasicProposalItem *> m_completions;
    TextEditor::SnippetAssistCollector m_snippetCollector;
    CPlusPlus::Icons m_icons;
    QStringList preprocessorCompletions;
    QScopedPointer<CppAssistProposalModel> m_model;
    TextEditor::IAssistProposal *m_hintProposal;
};

class CppCompletionAssistInterface : public TextEditor::DefaultAssistInterface
{
public:
    CppCompletionAssistInterface(const QString &filePath,
                                 QTextDocument *textDocument,
                                 bool isObjCEnabled,
                                 int position,
                                 TextEditor::AssistReason reason,
                                 const WorkingCopy &workingCopy)
        : TextEditor::DefaultAssistInterface(textDocument, position, filePath, reason)
        , m_isObjCEnabled(isObjCEnabled)
        , m_gotCppSpecifics(false)
        , m_workingCopy(workingCopy)
    {}

    CppCompletionAssistInterface(const QString &filePath,
                                 QTextDocument *textDocument,
                                 int position,
                                 TextEditor::AssistReason reason,
                                 const CPlusPlus::Snapshot &snapshot,
                                 const ProjectPart::HeaderPaths &headerPaths)
        : TextEditor::DefaultAssistInterface(textDocument, position, filePath, reason)
        , m_isObjCEnabled(false)
        , m_gotCppSpecifics(true)
        , m_snapshot(snapshot)
        , m_headerPaths(headerPaths)
    {}

    bool isObjCEnabled() const { return m_isObjCEnabled; }

    const CPlusPlus::Snapshot &snapshot() const { getCppSpecifics(); return m_snapshot; }
    const ProjectPart::HeaderPaths &headerPaths() const
    { getCppSpecifics(); return m_headerPaths; }

private:
    void getCppSpecifics() const;

    mutable bool m_isObjCEnabled;
    mutable bool m_gotCppSpecifics;
    WorkingCopy m_workingCopy;
    mutable CPlusPlus::Snapshot m_snapshot;
    mutable ProjectPart::HeaderPaths m_headerPaths;
};

} // Internal
} // CppTools

Q_DECLARE_METATYPE(CPlusPlus::Symbol *)

#endif // CPPCOMPLETIONASSIST_H
