# Before posting: the checks that need phones and routers

The automated checks come first (see README, Testing): `pio test -e native`, `api_check --mock`, and once
per firmware version `api_check --board` on one board, both on its setup WiFi
(`--board http://192.168.4.1`) and on home WiFi. Then `flash_check` on every board as it's flashed for
posting. What's below can't be automated: it needs real phones, real routers and real batteries. Use a
test name and test WiFi, then erase and flash again before posting.

Have to hand: an iPhone, an Android phone, the home router, a second router or a phone hotspot whose
settings you can change, three nearly flat AA batteries, and a `-ota.bin` with a different version (build
one with a temporary `FIRMWARE_VERSION` such as `2026.10.0-test`, or the update test can't show it worked).

## Two eyes (wired as the guide shows)

Board -> the viewer's right eye (the left one seen from behind) -> its DOUT header -> the other eye's DIN
header. If anything looks swapped, fix the wiring, not the code.

- [ ] Switch-on: the far eye has no stray dots and doesn't stay fully lit.
- [ ] Wink left and Wink right wink the right eyes; Cross-eyed turns the pupils in towards the nose.
- [ ] Your own words: upright, not mirrored, entering at the right eye's right edge and reaching the left
      eye as one readable stream.
- [ ] Brightness 0 and the top setting: both eyes change alike.
- [ ] Power, both eyes at the top brightness, WiFi on and a phone on the page: "Boo!" and a fruit-machine
      jackpot don't restart it on fresh AAs.

## Setup through the captive portal

- [ ] iPhone: join "Spooky Pumpkin" (password `spookyeyes`). The sign-in sheet opens the setup page by
      itself. Name it, pick the WiFi, tap Connect; "Your pumpkin is alive!" and "Please wait" show until
      the setup WiFi goes (the sheet may close then). The eyes scroll `http://pumpkin.local or <IP>`, and
      both open the buttons.
- [ ] In Safari at pumpkin.local instead: after Connect the page opens the buttons by itself (within 30 s).
- [ ] "Skip WiFi for now: just use the buttons" (under Connect) opens the buttons on the setup WiFi, and
      they work there.
- [ ] Android: the same, from the "Sign in to network" notification. Note whether `pumpkin.local` opens;
      the IP address must.
- [ ] Wrong password first: the check says the password didn't work and nothing is saved (the pumpkin
      stays on its setup WiFi). Then the right one saves.
- [ ] A password with symbols, as far as the router allows: `%`, `"`, `&`, `\`, `£`, a space, and one of
      exactly 63 characters. Each joins.
- [ ] A WiFi name with spaces, an apostrophe and an emoji (if the router allows): found in the list, joins.
- [ ] A hidden network, typed in by hand: joins.

## Routers

- [ ] WPA2 (the usual): joins, and rejoins after the router restarts.
- [ ] WPA2/WPA3 mixed ("transition"): joins.
- [ ] WPA3 only: note what happens and what the setup page says.
- [ ] Mesh, or several access points with the same name: joins, and stays on as you walk between them.
- [ ] A 5 GHz-only network: the setup page explains it needs 2.4 GHz rather than failing silently.

## Out and about

- [ ] Doorstep: on home WiFi with no page open for 10+ minutes, take it out of range (or switch the
      router's 2.4 GHz off) for over 3 minutes. "Spooky Pumpkin" appears and stays (it used to vanish at
      once); a phone that joins gets the buttons; checking or changing the WiFi asks for a BOOT press. Back
      in range within 10 minutes, it rejoins the home WiFi by itself; after 10 minutes unused its WiFi goes
      off, and switching it off and on brings it back to the home WiFi.
- [ ] Saving batteries: with no page open and no phone on its WiFi for 10 minutes, the WiFi goes off (the
      status LED goes out, the network disappears) and the show carries on. A short BOOT press brings it
      back. With the button page open on a phone it stays on; with "Save batteries" off in Settings too.
- [ ] Two phones on the button page at once: both follow what's playing, and both talk.
- [ ] Talking pumpkin: an iPhone (silent switch off) and an Android phone speak each button's phrase.
- [ ] Holding BOOT for 5 seconds forgets the WiFi: it restarts on its setup WiFi.

## Updates and power

- [ ] Firmware update from the Settings page: refused without a BOOT press; refused for the `-full.bin`;
      with BOOT and the `-ota.bin` it restarts with the new version (Settings shows it), keeping the name,
      WiFi, settings and saved show.
- [ ] Weak batteries, both eyes at the top brightness: on nearly flat AAs the eyes say "New batteries
      please" and it doesn't keep restarting. On fresh ones the WiFi works. On a USB power bank, the same.
- [ ] Left running for an evening (an hour or more) with phones coming and going: still answering, and
      `pumpkin_serial.py status` shows the free memory steady.

## Flashing the boards for posting

1. Once nothing else is being merged: in the main checkout (where `local/credits.h` is), `pio test -e
   native`, `api_check --mock`, `python3 scripts/build_release.py`, and tag the commit (`git tag
   2026.10.0`). Keep the images private (they include the credits).
2. For each board, including any used for testing (it has a test name and WiFi saved): `python3
   tools/flash_check.py release/spooky-pumpkin-eyes-<version>-full.bin --save-log boardN.log`. It refuses
   if another program has the serial port; every check must PASS. Look over the first board's log by hand.
3. With test eyes and a mouth LED attached: both eyes scroll "Join WiFi: Spooky Pumpkin ..."; on
   192.168.4.1 a few buttons work and the mouth shows red, green and blue; a short BOOT press repeats the
   message. Save nothing, then unplug.
4. Label each board with the version, and pack it with a printed guide.
