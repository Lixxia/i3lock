This is my own copy of i3lock, consisting of the following tweaks: 
- Display changes on key-strokes and escape/backspace.
- Added 12-hour clock to the unlock indicator and periodic updater so time stays relevant. 
- The unlock indicator will always be displayed, regardless of state. (Originally it was only shown after initial keypress) 
- **10/17/15** Added command line arguments to customize colors. Each (optional) argument will accept a color in hexadecimal format. 
  - `-o color` Specifies verification color
  - `-w color` Specifies wrong password/backspace color
  - `-l color` Specifies default/idle color
  - The given colors will be used as-is for the lines and text for their respective states. The colors will automatically be lightened slightly and used with lower opacity (20%) for the circle fill. 
  - If no colors are specified it defaults to green/red/black for verify/wrong/idle respectively.

## Example Usage
`i3lock -i ~/.i3/background.png -c '#000000' -o '#191d0f' -w '#572020' -l '#ffffff' -e`

## Screenshots
*TODO:* Add screenshots of default behavior. (No colors specified)
#### Default
![Default state](/screenshots/lockscreen.png?raw=true "")
#### Key Press
![On key press](/screenshots/lockscreenkeypress.png?raw=true "")
#### Escape/Backspace
![On escape or backspace](/screenshots/lockscreenesc.png?raw=true "")

<p>
---
### Original README

i3lock - improved screen locker
===============================
i3lock is a simple screen locker like slock. After starting it, you will
see a white screen (you can configure the color/an image). You can return
to your screen by entering your password.

Many little improvements have been made to i3lock over time:

- i3lock forks, so you can combine it with an alias to suspend to RAM
  (run "i3lock && echo mem > /sys/power/state" to get a locked screen
   after waking up your computer from suspend to RAM)

- You can specify either a background color or a PNG image which will be
  displayed while your screen is locked.

- You can specify whether i3lock should bell upon a wrong password.

- i3lock uses PAM and therefore is compatible with LDAP etc.

Requirements
------------
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libxcb-xinerama
- libev
- libx11-dev
- libx11-xcb-dev
- libxkbfile-dev
- libxkbcommon >= 0.4.0
- libxkbcommon-x11 >= 0.4.0

Running i3lock
-------------
Simply invoke the 'i3lock' command. To get out of it, enter your password and
press enter.

Upstream
--------
Please submit patches to http://cr.i3wm.org/
