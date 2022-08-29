include(include/definitions.pri)

TARGET = DigitalNote-qt
DIGITALNOTE_APP_NAME = app
DIGITALNOTE_PATH = $$PWD

## Custom Configurations
include(DigitalNote_config.pri)

## Base
include(include/base.pri)
include(include/build_path.pri)
include(include/compiler_settings.pri)
include(include/security.pri)
include(include/qt.pri)
include(include/release.pri)
include(include/msse2.pri)

## App settings
include(include/app/qt_settings.pri)
include(include/app/forums.pri)
include(include/app/headers.pri)
include(include/app/other_files.pri)
include(include/app/resources.pri)
include(include/app/sources.pri)
include(include/app/translations.pri)

## Compile Options
include(include/options/use_update_alternatives.pri)
include(include/options/use_0.pri)
include(include/options/use_build_info.pri)
include(include/options/use_dbus.pri)
include(include/options/use_pch.pri)
include(include/options/use_unity_build.pri)
include(include/options/use_limits_patch.pri)
include(include/options/release.pri)

## Libraries
include(include/libs.pri)

system($$QMAKE_LRELEASE -silent $$_PRO_FILE_)
