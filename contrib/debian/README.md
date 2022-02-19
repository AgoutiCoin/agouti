
Debian
====================
This directory contains files used to package agoutioldd/agoutiold-qt
for Debian-based Linux systems. If you compile agoutioldd/agoutiold-qt yourself, there are some useful files here.

## agoutiold: URI support ##


agoutiold-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install agoutiold-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your agoutioldqt binary to `/usr/bin`
and the `../../share/pixmaps/agoutiold128.png` to `/usr/share/pixmaps`

agoutiold-qt.protocol (KDE)

