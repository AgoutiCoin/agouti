
Debian
====================
This directory contains files used to package agoutid/agouti-qt
for Debian-based Linux systems. If you compile agoutid/agouti-qt yourself, there are some useful files here.

## agouti: URI support ##


agouti-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install agouti-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your agoutiqt binary to `/usr/bin`
and the `../../share/pixmaps/agouti128.png` to `/usr/share/pixmaps`

agouti-qt.protocol (KDE)

