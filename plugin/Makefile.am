desktopdir = $(datadir)/applications
desktop_in_files = xfce-xfcalendar-settings.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = 								\
	$(desktop_in_files)

DISTCLEANFILES =							\
	$(desktop_DATA)

