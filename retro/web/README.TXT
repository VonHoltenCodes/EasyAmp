EASYAMP RETRO
For Windows 98 SE through Windows XP
====================================

Two ways to have it:

  * EasyAmp-Retro-Setup.exe  - the installer. Program Files, Start Menu and
    desktop shortcuts, and an entry in Add/Remove Programs.
  * EASYAMP.EXE on its own   - portable. Runs from any folder, a USB stick
    or a floppy; nothing to install. To remove it, delete the folder.

Either way it keeps its settings (EASYAMP.INI) and playlist (EASYAMP.M3U)
beside the program. If that folder cannot be written (a limited account on
XP), it uses Application Data\EasyAmp instead.


GETTING IT ONTO THE OLD PC
--------------------------
A Windows 98 or XP machine cannot download from the modern web: those sites
are encrypted in ways its browser never learned. Two ways round that:

  * On the old PC, open Internet Explorer and type:
        http://dl.easyampstereo.com/retro/
    That address is plain, unencrypted HTTP, made for exactly this.

  * Or download on a modern computer and carry it over: a USB stick, a CD-R,
    a network share, or a floppy disk. Either download fits on one 1.44 MB disk.


FIRST RUN
---------
  1. Run the installer and start EasyAmp from the Start Menu - or put
     EASYAMP.EXE in a folder of its own, for example C:\EASYAMP, and run it.
  2. Press +FILE, or drag music onto the window.
     It plays MP3, FLAC, Ogg Vorbis and WAV. Double-click a track to play.
     Ctrl-click and Shift-click select several. LOAD and SAVE use .m3u files.


PLEX
----
  1. Open the SOURCES tab and press +PLEX.
  2. EasyAmp shows a four-character code. The zero has a slash through it.
  3. ON YOUR PHONE (or any modern computer), go to   plex.tv/link
     and type the code in. Codes last fifteen minutes.
  4. Your library appears. Browse to an artist, album or track, then
     PLAY, ADD, or ADD ALL.

  Your Plex server must allow plain local connections:
      Settings > Network > Secure connections = Preferred
  That is Plex's default. Only the link step is encrypted; the music travels
  plainly across your own network, which is what lets an old processor keep up.


JELLYFIN
--------
  Press +JELLYFIN and type your server's address (for example
  192.168.1.5:8096), your user name and your password.
  The password is used once and is never saved.


THE EQUALIZER
-------------
  The EQUALIZER tab is a 10 to 32 band parametric EQ with presets, BASS and
  LOUD, balance and pitch. IMPORT and EXPORT read and write Equalizer APO
  config files and AutoEQ "GraphicEQ" lines, so a curve made for your
  headphones on a modern PC works here too.


WHAT IT NEEDS
-------------
  Windows 98 SE through XP (tested on 98 SE and XP SP3, on real hardware).
  A Pentium II or Celeron and up. About 10 MB of memory.
  Any colour depth from 16 colours. A network only for Plex and Jellyfin.
  The PC's clock does not have to be right.


IF SOMETHING IS OFF
-------------------
  Red light beside a server
      The PC cannot reach it. Check the cable, and that the server allows
      plain local connections.
  The Plex code will not link
      Press +PLEX again for a fresh code.
  Colours look wrong in 256-colour mode
      Click the EasyAmp window. Windows gives the palette to the program
      in front.
  EASYAMP.INI holds your server sign-in tokens in plain text, because
      Windows 98 has no protected place to keep them. Treat the folder
      accordingly. REM on the SOURCES tab forgets an account.


FREE SOFTWARE
-------------
  MIT licensed, open source, and always free.
  Source, issues and the full story:
      https://github.com/VonHoltenCodes/EasyAmp/tree/main/retro
      https://www.easyampstereo.com/retro.html
