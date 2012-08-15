# http://qt-project.org/doc/qt-4.8/qmake-function-reference.html

TEMPLATE = app

CONFIG += qt warn_on debug_and_release cascades

device {
	ARCH = armle-v7
	CONFIG(release, debug|release) {
		DESTDIR = o.le-v7
	}
	CONFIG(debug, debug|release) {
		DESTDIR = o.le-v7-g
	}
}

simulator {
	ARCH = x86
	LIBS += -lsocket -lz -lbz2
	CONFIG(release, debug|release) {
		DESTDIR = o
	}
	CONFIG(debug, debug|release) {
		DESTDIR = o-g
	}
}

include($${TARGET}.pri)
INCLUDEPATH += ../ffmpeg/include
LIBS += -lcamapi -lscreen -L../ffmpeg/lib/$${ARCH} -lavformat -lavcodec -lavutil

OBJECTS_DIR = $${DESTDIR}/.obj
MOC_DIR = $${DESTDIR}/.moc
RCC_DIR = $${DESTDIR}/.rcc
UI_DIR = $${DESTDIR}/.ui

suredelete.target = sureclean
suredelete.commands = $(DEL_FILE) $${MOC_DIR}/*; $(DEL_FILE) $${RCC_DIR}/*; $(DEL_FILE) $${UI_DIR}/*
suredelete.depends = distclean

QMAKE_EXTRA_TARGETS += suredelete