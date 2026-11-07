# src layout - see PLAN.md
add_subdirectory(core)
add_subdirectory(interfaces)
add_subdirectory(ui)
add_subdirectory(app)
add_subdirectory(platform/qt)
add_subdirectory(platform/wl)
add_subdirectory(platform/fdo)
add_subdirectory(platform/evdev)

# ---- which DE shell builds the `traduku` executable (ROADMAP.md §2) ---
# KF6 is probed first (QUIET, never fatal here), then LT_SHELL decides.
set(LT_KDE_AVAILABLE FALSE)
if(NOT LT_SHELL STREQUAL "gnome")
    find_package(KF6CoreAddons QUIET)
    find_package(KF6Config QUIET)
    find_package(KF6DBusAddons QUIET)
    find_package(KF6I18n QUIET)
    find_package(KF6GlobalAccel QUIET)
    find_package(KF6Notifications QUIET)
    if(KF6CoreAddons_FOUND AND KF6Config_FOUND AND KF6DBusAddons_FOUND
       AND KF6I18n_FOUND
       AND KF6GlobalAccel_FOUND AND KF6Notifications_FOUND)
        set(LT_KDE_AVAILABLE TRUE)
    endif()
endif()

if(LT_SHELL STREQUAL "kde")
    if(NOT LT_KDE_AVAILABLE)
        message(FATAL_ERROR
            "LT_SHELL=kde but the KF6 stack (CoreAddons, Config, DBusAddons, "
            "I18n, GlobalAccel, Notifications) is not "
            "installed. Install it or build the other shell with "
            "-DLT_SHELL=gnome.")
    endif()
    set(LT_SHELL_EFFECTIVE "kde")
elseif(LT_SHELL STREQUAL "gnome")
    set(LT_SHELL_EFFECTIVE "gnome")
else() # auto: keep the historical behaviour - KDE when KF6 is present
    if(LT_BUILD_KDE AND LT_KDE_AVAILABLE)
        set(LT_SHELL_EFFECTIVE "kde")
    else()
        set(LT_SHELL_EFFECTIVE "gnome")
    endif()
endif()

if(LT_BUILD_X11)
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xtst xi)
    add_subdirectory(platform/x11)
endif()

if(LT_SHELL_EFFECTIVE STREQUAL "kde")
    add_subdirectory(platform/kde)
    add_subdirectory(app/kde)
else()
    add_subdirectory(app/gnome)
endif()
set(LT_SHELL_EFFECTIVE "${LT_SHELL_EFFECTIVE}" PARENT_SCOPE)
message(STATUS "Traduku shell: ${LT_SHELL_EFFECTIVE} "
               "(LT_SHELL=${LT_SHELL}, KF6 found=${LT_KDE_AVAILABLE})")
