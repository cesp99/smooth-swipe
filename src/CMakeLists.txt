# Effect plugin
set(effect_SOURCES
    main.cpp
    smoothswipe.cpp
    springmotion.cpp
)

kconfig_add_kcfg_files(effect_SOURCES
    smoothswipeconfig.kcfgc
)

add_library(kwin_effect_smoothswipe MODULE ${effect_SOURCES})
# The effect's plugin id is derived from the .so basename; it must be
# "smoothswipe" to match the kwinrc enable key and the KCM's reconfigure call.
set_target_properties(kwin_effect_smoothswipe PROPERTIES OUTPUT_NAME "smoothswipe")
target_compile_definitions(kwin_effect_smoothswipe PRIVATE
    KPLUGINFACTORY_PLUGIN_CLASS_INTERNAL_NAME=kwin_effect_smoothswipe_factory
)
target_link_libraries(kwin_effect_smoothswipe PRIVATE
    KWin::kwin
    KF6::ConfigGui
    Qt6::Core
    Qt6::Gui
)
install(TARGETS kwin_effect_smoothswipe DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)

# Config module (KCM)
set(kcm_SOURCES smoothswipe_config.cpp)
ki18n_wrap_ui(kcm_SOURCES smoothswipe_config.ui)
kconfig_add_kcfg_files(kcm_SOURCES smoothswipeconfig.kcfgc)
qt_add_dbus_interface(kcm_SOURCES ${KDE_INSTALL_FULL_DBUSINTERFACEDIR}/org.kde.kwin.Effects.xml kwineffects_interface)

add_library(kwin_smoothswipe_config MODULE ${kcm_SOURCES})
target_compile_definitions(kwin_smoothswipe_config PRIVATE
    KPLUGINFACTORY_PLUGIN_CLASS_INTERNAL_NAME=kwin_smoothswipe_config_factory
)
target_link_libraries(kwin_smoothswipe_config PRIVATE
    KF6::KCMUtils
    KF6::CoreAddons
    KF6::ConfigGui
    KF6::I18n
    Qt6::DBus
    Qt6::Widgets
)
install(TARGETS kwin_smoothswipe_config DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/configs)
