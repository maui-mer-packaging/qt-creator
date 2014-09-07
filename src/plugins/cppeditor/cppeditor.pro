DEFINES += CPPEDITOR_LIBRARY
include(../../qtcreatorplugin.pri)

HEADERS += \
    cppautocompleter.h \
    cppcanonicalsymbol.h \
    cppclasswizard.h \
    cppcodemodelinspectordialog.h \
    cppdocumentationcommenthelper.h \
    cppeditor.h \
    cppeditor_global.h \
    cppeditordocument.h \
    cppeditorconstants.h \
    cppeditorenums.h \
    cppeditoroutline.h \
    cppeditorplugin.h \
    cppelementevaluator.h \
    cppfilewizard.h \
    cppfollowsymbolundercursor.h \
    cppfunctiondecldeflink.h \
    cpphighlighter.h \
    cpphoverhandler.h \
    cppincludehierarchy.h \
    cppincludehierarchyitem.h \
    cppincludehierarchymodel.h \
    cppincludehierarchytreeview.h \
    cppinsertvirtualmethods.h \
    cpplocalrenaming.h \
    cppoutline.h \
    cpppreprocessordialog.h \
    cppquickfix.h \
    cppquickfixassistant.h \
    cppquickfixes.h \
    cppsnippetprovider.h \
    cpptypehierarchy.h \
    cppuseselectionsupdater.h \
    cppvirtualfunctionassistprovider.h \
    cppvirtualfunctionproposalitem.h

SOURCES += \
    cppautocompleter.cpp \
    cppcanonicalsymbol.cpp \
    cppclasswizard.cpp \
    cppcodemodelinspectordialog.cpp \
    cppdocumentationcommenthelper.cpp \
    cppeditor.cpp \
    cppeditordocument.cpp \
    cppeditoroutline.cpp \
    cppeditorplugin.cpp \
    cppelementevaluator.cpp \
    cppfilewizard.cpp \
    cppfollowsymbolundercursor.cpp \
    cppfunctiondecldeflink.cpp \
    cpphighlighter.cpp \
    cpphoverhandler.cpp \
    cppincludehierarchy.cpp \
    cppincludehierarchyitem.cpp \
    cppincludehierarchymodel.cpp \
    cppincludehierarchytreeview.cpp \
    cppinsertvirtualmethods.cpp \
    cpplocalrenaming.cpp \
    cppoutline.cpp \
    cpppreprocessordialog.cpp \
    cppquickfix.cpp \
    cppquickfixassistant.cpp \
    cppquickfixes.cpp \
    cppsnippetprovider.cpp \
    cpptypehierarchy.cpp \
    cppuseselectionsupdater.cpp \
    cppvirtualfunctionassistprovider.cpp \
    cppvirtualfunctionproposalitem.cpp

FORMS += \
    cpppreprocessordialog.ui \
    cppcodemodelinspectordialog.ui

RESOURCES += \
    cppeditor.qrc

equals(TEST, 1) {
    HEADERS += \
        cppeditortestcase.h \
        cppquickfix_test.h
    SOURCES += \
        cppdoxygen_test.cpp \
        cppeditortestcase.cpp \
        cppincludehierarchy_test.cpp \
        cppquickfix_test.cpp \
        cppuseselections_test.cpp \
        fileandtokenactions_test.cpp \
        followsymbol_switchmethoddecldef_test.cpp
    DEFINES += SRCDIR=\\\"$$PWD\\\"
}
