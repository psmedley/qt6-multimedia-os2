# Generated from multimediawidgets.pro.

#####################################################################
## MultimediaWidgets Module:
#####################################################################

qt_internal_add_module(MultimediaWidgets
    SOURCES
        qtmultimediawidgetdefs.h
        qvideowidget.cpp qvideowidget.h qvideowidget_p.h
    LIBRARIES
        Qt::MultimediaPrivate
        Qt::WidgetsPrivate
    PUBLIC_LIBRARIES
        Qt::Core
        Qt::Gui
        Qt::Multimedia
        Qt::Widgets
    PRIVATE_MODULE_INTERFACE
        Qt::MultimediaPrivate
        Qt::WidgetsPrivate
    OS2_SHORT_NAME Qt6MMWid
)

## Scopes:
#####################################################################

qt_internal_extend_target(MultimediaWidgets CONDITION QT_FEATURE_graphicsview
    SOURCES
        qgraphicsvideoitem.cpp qgraphicsvideoitem.h
)
