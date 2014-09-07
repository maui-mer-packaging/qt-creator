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

#ifndef CPPEDITOR_H
#define CPPEDITOR_H

#include "cppfunctiondecldeflink.h"

#include <texteditor/basetexteditor.h>

#include <utils/qtcoverride.h>

#include <QScopedPointer>

namespace CPlusPlus { class Symbol; }
namespace CppTools { class SemanticInfo; }

namespace CppEditor {
namespace Internal {

class CppEditorDocument;
class CppEditorOutline;
class CppEditorWidgetPrivate;
class FollowSymbolUnderCursor;
class FunctionDeclDefLink;

class CppEditor : public TextEditor::BaseTextEditor
{
    Q_OBJECT

public:
    CppEditor();
};

class CppEditorWidget : public TextEditor::BaseTextEditorWidget
{
    Q_OBJECT

public:
    static Link linkToSymbol(CPlusPlus::Symbol *symbol);

public:
    CppEditorWidget();
    ~CppEditorWidget();

    CppEditorDocument *cppEditorDocument() const;
    CppEditorOutline *outline() const;

    CppTools::SemanticInfo semanticInfo() const;
    bool isSemanticInfoValidExceptLocalUses() const;
    bool isSemanticInfoValid() const;

    QSharedPointer<FunctionDeclDefLink> declDefLink() const;
    void applyDeclDefLinkChanges(bool jumpToMatch);

    TextEditor::IAssistInterface *createAssistInterface(
            TextEditor::AssistKind kind,
            TextEditor::AssistReason reason) const QTC_OVERRIDE;

    FollowSymbolUnderCursor *followSymbolUnderCursorDelegate(); // exposed for tests
    TextEditor::CompletionAssistProvider *completionAssistProvider() const QTC_OVERRIDE;

public slots:
    void paste() QTC_OVERRIDE;
    void cut() QTC_OVERRIDE;
    void selectAll() QTC_OVERRIDE;

    void switchDeclarationDefinition(bool inNextSplit);
    void showPreProcessorWidget();

    void findUsages();
    void renameSymbolUnderCursor();
    void renameUsages(const QString &replacement = QString());

protected:
    bool event(QEvent *e) QTC_OVERRIDE;
    void contextMenuEvent(QContextMenuEvent *) QTC_OVERRIDE;
    void keyPressEvent(QKeyEvent *e) QTC_OVERRIDE;

    void applyFontSettings() QTC_OVERRIDE;

    bool openLink(const Link &link, bool inNextSplit) QTC_OVERRIDE
    { return openCppEditorAt(link, inNextSplit); }

    Link findLinkAt(const QTextCursor &, bool resolveTarget = true,
                    bool inNextSplit = false) QTC_OVERRIDE;

    void onRefactorMarkerClicked(const TextEditor::RefactorMarker &marker) QTC_OVERRIDE;

protected slots:
    void slotCodeStyleSettingsChanged(const QVariant &) QTC_OVERRIDE;

private slots:
    void updateFunctionDeclDefLink();
    void updateFunctionDeclDefLinkNow();
    void abortDeclDefLink();
    void onFunctionDeclDefLinkFound(QSharedPointer<FunctionDeclDefLink> link);

    void onFilePathChanged();
    void onCppDocumentUpdated();

    void onCodeWarningsUpdated(unsigned revision,
                               const QList<QTextEdit::ExtraSelection> selections);
    void onIfdefedOutBlocksUpdated(unsigned revision,
                                   const QList<TextEditor::BlockRange> ifdefedOutBlocks);

    void updateSemanticInfo(const CppTools::SemanticInfo &semanticInfo,
                            bool updateUseSelectionSynchronously = false);
    void updatePreprocessorButtonTooltip();

    void performQuickFix(int index);

    void processKeyNormally(QKeyEvent *e);

private:
    void finalizeInitialization() QTC_OVERRIDE;
    void finalizeInitializationAfterDuplication(BaseTextEditorWidget *other) QTC_OVERRIDE;

    static bool openCppEditorAt(const Link &, bool inNextSplit = false);

    unsigned documentRevision() const;

private:
    QScopedPointer<CppEditorWidgetPrivate> d;
};

} // namespace Internal
} // namespace CppEditor

#endif // CPPEDITOR_H
