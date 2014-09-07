import qbs 1.0

import QtcPlugin

QtcPlugin {
    name: "FakeVim"

    Depends { name: "Qt.widgets" }
    Depends { name: "Aggregation" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "TextEditor" }

    files: [
        "fakevim.qrc",
        "fakevimactions.cpp",
        "fakevimactions.h",
        "fakevimhandler.cpp",
        "fakevimhandler.h",
        "fakevimoptions.ui",
        "fakevimplugin.cpp",
        "fakevimplugin.h",
        "fakevimtr.h",
    ]

    Group {
        name: "Tests"
        condition: project.testsEnabled
        files: ["fakevim_test.cpp"]
    }
}
