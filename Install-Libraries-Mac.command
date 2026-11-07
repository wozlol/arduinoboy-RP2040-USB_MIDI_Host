#!/bin/bash
# Arduinoboy Host Edition, one-time library setup for the Arduino IDE on a Mac.
# It backs up your own copies of two libraries, then installs the edited copies from this repository.

REPO="$(cd "$(dirname "$0")" && pwd)"
SRC="$REPO/Arduinoboy/libraries"
LIBS="${ARDUINO_LIBRARIES_DIR:-$HOME/Documents/Arduino/libraries}"
BACKUPS="${ARDUINO_LIBRARY_BACKUP_DIR:-$HOME/Documents/Arduino_library_backups}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OURS="Adafruit_TinyUSB_Library Pico_PIO_USB"

pause() { [ -n "$ARDUINOBOY_INSTALL_YES" ] || read -r -p "Press Enter to close this window. " _ || true; }
libname() { grep -m1 '^name=' "$1/library.properties" 2>/dev/null | cut -d= -f2- | tr -d '\r' | sed 's/[[:space:]]*$//'; }

echo "Arduinoboy Host Edition, library setup"
echo

for d in $OURS; do
  if [ ! -d "$SRC/$d" ]; then
    echo "Could not find $SRC/$d"
    echo "Keep this file in the Arduinoboy folder you downloaded, next to the Arduinoboy folder, and try again."
    pause; exit 1
  fi
done

already=1
for d in $OURS; do
  diff -rq "$SRC/$d" "$LIBS/$d" >/dev/null 2>&1 || already=0
done
if [ "$already" = 1 ]; then
  echo "Already set up. Your Arduino libraries folder has the edited copies, nothing to do."
  pause; exit 0
fi

# Work out what will be moved aside: your copies with our folder names, and any other
# folder that holds the same library under a different folder name (Arduino could pick those instead).
MOVE=""
for d in $OURS; do
  [ -e "$LIBS/$d" ] && MOVE="$MOVE|$d"
  want="$(libname "$SRC/$d")"
  if [ -d "$LIBS" ] && [ -n "$want" ]; then
    for other in "$LIBS"/*/; do
      other="$(basename "$other")"
      [ "$other" = "$d" ] && continue
      [ "$(libname "$LIBS/$other")" = "$want" ] && MOVE="$MOVE|$other"
    done
  fi
done
MOVE="${MOVE#|}"

echo "Arduino libraries folder: $LIBS"
echo
if [ -n "$MOVE" ]; then
  echo "These folders are your own copies of the same libraries. They will be moved to a backup folder first:"
  echo "$MOVE" | tr '|' '\n' | sed 's/^/   /'
  echo "Backup folder: $BACKUPS/$STAMP"
else
  echo "You have no copies of these libraries yet, so there is nothing to back up."
fi
echo
echo "Then these two edited libraries are copied in:"
for d in $OURS; do echo "   $d"; done
echo
echo "Heads up: this changes those two libraries for all your Arduino projects."
echo "Our USB library turns off USB host support for keyboards, storage and serial."
echo

if [ -z "$ARDUINOBOY_INSTALL_YES" ]; then
  read -r -p "Type y and press Enter to continue, or just close this window to cancel: " ans
  case "$ans" in y|Y|yes|YES) ;; *) echo "Cancelled. Nothing was changed."; exit 0;; esac
fi

mkdir -p "$LIBS" || { echo "Could not create $LIBS"; pause; exit 1; }

if [ -n "$MOVE" ]; then
  mkdir -p "$BACKUPS/$STAMP" || { echo "Could not create the backup folder, nothing was changed."; pause; exit 1; }
  echo "$MOVE" | tr '|' '\n' | while IFS= read -r f; do
    mv "$LIBS/$f" "$BACKUPS/$STAMP/$f" || { echo "Could not move $f, stopping."; exit 1; }
  done || { pause; exit 1; }
  cat > "$BACKUPS/$STAMP/HOW_TO_PUT_THEM_BACK.txt" <<EOF
These folders were moved here from $LIBS on $STAMP by Install-Libraries-Mac.command.
To undo: delete the folders $OURS from $LIBS, then move the folders in this backup folder back into $LIBS.
EOF
fi

for d in $OURS; do
  cp -R "$SRC/$d" "$LIBS/$d" || { echo "Could not copy $d, stopping."; pause; exit 1; }
done

if grep -q ARDUINOBOY_MODIFIED_TINYUSB "$LIBS/Adafruit_TinyUSB_Library/src/arduino/ports/rp2040/tusb_config_rp2040.h" 2>/dev/null; then
  echo
  echo "Done. Open Arduinoboy/Arduinoboy.ino in the Arduino IDE and press Verify."
  [ -n "$MOVE" ] && echo "Your own copies are safe in: $BACKUPS/$STAMP"
else
  echo
  echo "The copy finished but the check failed. Please tell us what folder you used: $LIBS"
fi
pause
