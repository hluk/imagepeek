ImagePeek
=========

ImagePeek is simple image viewer.

Usage: imagepeek [images...]

Browse images passed as command line arguments.

Shortcuts
---------

* **Arrow keys, Page Up, Page Down, Spacebar**: basic navigation
* **N, J, Enter**: next item
* **P, K, SHIFT + N**: previous item
* **SHIFT + Home or SHIFT + End**: first or last item
* **\+ or -**: zoom in or out
* **number key**: set zoom level (keypad 2 is 2x zoom)
* **CTRL + number key**: set zoom level (keypad 2 is 1/2x zoom)
* **W or H**: fit to width or height
* **0 or /**: zoom to fit
* **\* or 1**: original zoom (1x)
* **A/Z**: sharpen more/less
* **F**: toggle fullscreen
* **C or R**: increase number of columns/rows
* **SHIFT + C or SHIFT + R**: decrease number of columns/rows
* **F5**: reload
* **Escape, Q**: exit

Sessions
--------

To save session set environment variable `IMAGEPEEK_SESSION` to session filename.
For example (save session in `imagepeek.ini` file):

    IMAGEPEEK_SESSION=imagepeek.ini imagepeek *.png

and to restore the session run:

    IMAGEPEEK_SESSION=imagepeek.ini imagepeek

(optinally specify other image filenames).

