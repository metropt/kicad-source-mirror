# the map generation creates on Windows/gcc a lot of useless warnings
# so disable it on windows
if( WIN32 AND NOT CMAKE_CROSSCOMPILING )
    set( MAKE_LINK_MAPS false )
else()
    set( MAKE_LINK_MAPS true )
endif()

add_definitions( -DCVPCB )

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ./dialogs
    ../3d-viewer
    ../pcbnew
    ../pcbnew/dialogs
    ../polygon
    ../common
    ${INC_AFTER}
    )


set( CVPCB_DIALOGS

    # These 2 still use search paths, which don't exist in footprint land.
#    dialogs/dialog_cvpcb_config.cpp
#    dialogs/dialog_cvpcb_config_fbp.cpp

    dialogs/dialog_display_options.cpp
    dialogs/dialog_display_options_base.cpp
    ../pcbnew/dialogs/dialog_fp_lib_table.cpp
    ../pcbnew/dialogs/dialog_fp_lib_table_base.cpp
    ../pcbnew/dialogs/dialog_fp_plugin_options.cpp
    ../pcbnew/dialogs/dialog_fp_plugin_options_base.cpp
    )

set( CVPCB_SRCS
    ../common/base_units.cpp
    ../pcbnew/board_items_to_polygon_shape_transform.cpp
    ../pcbnew/class_drc_item.cpp
    autosel.cpp
    cfg.cpp
    class_components_listbox.cpp
    class_DisplayFootprintsFrame.cpp
    class_footprints_listbox.cpp
    class_library_listbox.cpp
    cvframe.cpp
    listboxes.cpp
    menubar.cpp
    readwrite_dlgs.cpp
    tool_cvpcb.cpp
    )


if( MINGW )
    # CVPCB_RESOURCES variable is set by the macro.
    mingw_resource_compiler( cvpcb )
endif()


if( APPLE )
    set( CVPCB_RESOURCES cvpcb.icns cvpcb_doc.icns )

    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/cvpcb.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )

    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/cvpcb_doc.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )

    set( MACOSX_BUNDLE_ICON_FILE cvpcb.icns )
    set( MACOSX_BUNDLE_GUI_IDENTIFIER org.kicad-eda.cvpcb )
endif()


if( USE_KIWAY_DLLS )
    add_executable( cvpcb WIN32 MACOSX_BUNDLE
        ../common/single_top.cpp
        ../common/pgm_base.cpp
        ${CVPCB_RESOURCES}
        )
    set_source_files_properties( ../common/single_top.cpp PROPERTIES
        COMPILE_DEFINITIONS "TOP_FRAME=CVPCB_FRAME_TYPE;PGM_DATA_FILE_EXT=\"net\";BUILD_KIWAY_DLL"
        )
    target_link_libraries( cvpcb
        #singletop  # replaces common, giving us restrictive control and link warnings.
        # There's way too much crap coming in from common yet.
        common
        bitmaps
        ${wxWidgets_LIBRARIES}
        )
    if( MAKE_LINK_MAPS )
        set_target_properties( cvpcb PROPERTIES
            LINK_FLAGS "${TO_LINKER},-cref ${TO_LINKER},-Map=cvpcb.map" )
    endif()

    # the main cvpcb program, in DSO form.
    add_library( cvpcb_kiface MODULE
        cvpcb.cpp
        ${CVPCB_SRCS}
        ${CVPCB_DIALOGS}
#        ${CVPCB_RESOURCES}
        )
    set_target_properties( cvpcb_kiface PROPERTIES
        OUTPUT_NAME     cvpcb
        PREFIX          ${KIFACE_PREFIX}
        SUFFIX          ${KIFACE_SUFFIX}
        )
    target_link_libraries( cvpcb_kiface
        3d-viewer
        pcbcommon
        pcad2kicadpcb
        common
        bitmaps
        polygon
        gal
        ${wxWidgets_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${GDI_PLUS_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PIXMAN_LIBRARY}
        ${OPENMP_LIBRARIES}
        )

    # Only for win32 cross compilation using MXE
    if( WIN32 AND MSYS AND CMAKE_CROSSCOMPILING )
        target_link_libraries( cvpcb_kiface
            opengl32
            glu32
            pixman-1
            fontconfig
            freetype
            bz2
            )
    endif()

    if( BUILD_GITHUB_PLUGIN )
        target_link_libraries( cvpcb_kiface github_plugin )
    endif()

    # Must follow github_plugin
    target_link_libraries( cvpcb_kiface ${Boost_LIBRARIES} )

    if( UNIX AND NOT APPLE )
        # -lrt must follow Boost
        target_link_libraries( cvpcb_kiface rt )
    endif()

    if( APPLE )
        set_target_properties( cvpcb PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
            )
    endif()

    set_source_files_properties( cvpcb.cpp PROPERTIES
        # The KIFACE is in cvpcb.cpp, export it:
        COMPILE_DEFINITIONS     "BUILD_KIWAY_DLL;COMPILING_DLL"
        )

    if( MAKE_LINK_MAPS )
        set_target_properties( cvpcb_kiface PROPERTIES
            LINK_FLAGS "${TO_LINKER},-cref ${TO_LINKER},-Map=_cvpcb.kiface.map" )
    endif()

    # if building cvpcb, then also build cvpcb_kiface if out of date.
    add_dependencies( cvpcb cvpcb_kiface )

    # these 2 binaries are a matched set, keep them together:
    install( TARGETS cvpcb
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )
    install( TARGETS cvpcb_kiface
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )

    if( APPLE )
        # copies kiface into the bundle
        add_custom_target( _cvpcb_kiface_copy ALL
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cvpcb/_cvpcb.kiface "${CMAKE_BINARY_DIR}/cvpcb/cvpcb.app/Contents/MacOS/"
            DEPENDS cvpcb_kiface
            COMMENT "Copying kiface into cvpcb"
        )
    endif()

else()

    add_executable( cvpcb WIN32 MACOSX_BUNDLE
        ${CVPCB_SRCS}
        ${CVPCB_DIALOGS}
        ${CVPCB_RESOURCES}
        )
    target_link_libraries( cvpcb
        3d-viewer
        pcbcommon
        pcad2kicadpcb
        common
        bitmaps
        polygon
        gal
        ${wxWidgets_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${GDI_PLUS_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PIXMAN_LIBRARY}
        )

    # Only for win32 cross compilation using MXE
    if( WIN32 AND MSYS AND CMAKE_CROSSCOMPILING )
    target_link_libraries( cvpcb
        opengl32
        glu32
        pixman-1
        fontconfig
        freetype
        bz2
        )
    endif()

    if( BUILD_GITHUB_PLUGIN )
        target_link_libraries( cvpcb github_plugin )
    endif()

    # Must follow github_plugin
    target_link_libraries( cvpcb ${Boost_LIBRARIES} )

    if( APPLE )
        set_target_properties( cvpcb PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
            )
    endif()

    install( TARGETS cvpcb
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )

endif()
