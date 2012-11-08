# -------------------------------------------------
# Project created by QtCreator 2010-04-21T15:51:26
# -------------------------------------------------

TARGET = Eks3D
TEMPLATE = lib

include("../../EksCore/GeneralOptions.pri")

QT += opengl \
    xml

SOURCES += ../src/XDoodad.cpp \
    ../src/XScene.cpp \
    ../src/XCuboid.cpp \
    ../src/XTransform.cpp \
    ../src/XRenderer.cpp \
    ../src/XFrameEvent.cpp \
    ../src/XTransformEvent.cpp \
    ../src/XCamera.cpp \
    ../src/XGeometry.cpp \
    ../src/XShader.cpp \
    ../src/XTexture.cpp \
    ../src/XModeller.cpp \
    ../src/XColladaFile.cpp \
    ../src/XShape.cpp \
    ../src/XFrustum.cpp \
    ../src/XPlane.cpp \
    ../src/XLine.cpp \
    ../src/XTriangle.cpp \
    ../src/XFramebuffer.cpp \
    ../src/XLightManager.cpp \
    ../src/XLightRig.cpp \
    ../src/XAbstractCanvas.cpp \
    ../src/X2DCanvas.cpp \
    ../src/XAbstractRenderModel.cpp \
    ../src/XAbstractDelegate.cpp \
    ../src/XAbstractCanvasController.cpp \
    ../src/XCameraCanvasController.cpp \
    ../src/XObjLoader.cpp \
    ../src/XD3DRendererImpl.cpp

HEADERS += ../include/XDoodad.h \
    ../include/X3DGlobal.h \
    ../include/XScene.h \
    ../include/XCuboid.h \
    ../include/XTransform.h \
    ../include/XRenderer.h \
    ../include/XFrameEvent.h \
    ../include/XTransformEvent.h \
    ../include/XCamera.h \
    ../include/XGeometry.h \
    ../include/XShader.h \
    ../include/XTexture.h \
    ../include/XModeller.h \
    ../include/XColladaFile.h \
    ../include/XShape.h \
    ../include/XFrustum.h \
    ../include/XPlane.h \
    ../include/XLine.h \
    ../include/XTriangle.h \
    ../include/XFramebuffer.h \
    ../include/XLightManager.h \
    ../include/XLightRig.h \
    ../include/XAbstractCanvas.h \
    ../include/X2DCanvas.h \
    ../include/XAbstractRenderModel.h \
    ../include/XAbstractDelegate.h \
    ../include/XAbstractCanvasController.h \
    ../include/XCameraCanvasController.h \
    ../include/XObjLoader.h \
    ../include/XD3DRendererImpl.h


INCLUDEPATH += ../include/ \
    $$ROOT/Eks/EksCore/


win32-arm-msvc2012 {
  SOURCES += ../src/XD3DRenderer.cpp

  HEADERS += ../include/XD3DRenderer.h

  LIBS += -ld2d1 -ld3d11 -ldxgi -lwindowscodecs -ldwrite
} else {
  INCLUDEPATH += ../3rdParty
  DEFINES += GLEW_STATIC
  SOURCES += ../3rdParty/GL/glew.c \
             ../src/XGLRenderer.cpp \
             ../src/X3DCanvas.cpp

  HEADERS += ../include/XGLRenderer.h \
             ../include/X3DCanvas.h
}

LIBS += -lEksCore

RESOURCES += \
    ../GLResources.qrc

OTHER_FILES += \
    ../GLResources/shaders/default.vert \
    ../GLResources/shaders/default.frag \
    ../GLResources/shaders/blinn.vert \
    ../GLResources/shaders/blinn.frag \
    ../GLResources/shaders/plainColour.frag \
    ../GLResources/shaders/plainColour.vert \
    ../GLResources/shaders/standardSurface.frag \
    ../GLResources/shaders/standardSurface.vert

















