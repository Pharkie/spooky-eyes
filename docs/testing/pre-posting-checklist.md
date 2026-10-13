# Before posting: the checks that need phones and routers

The automated checks come first (see README, Testing): `pio test -e native`, `api_check --mock`, and once
per firmware version `api_check --board` on one board, both on its setup WiFi
(`--board http://192.168.4.1`) and on home WiFi. Then `flash_check` on every board as it's flashed for
posting. What's below can't be automated: it needs real phones, real routers and real batteries. Use a
test name and test WiFi, then erase and flash again before posting.

Have to hand: an iPhone, an Android phone, the home router, a second router or a phone hotspot whose
settings you can change, three nearly flat AA batteries, and the `-ota.bin` of the version being posted.

## Setup through the captive portal

- [ ] iPhone: join "Spooky Pumpkin" (password `spookyeyes`). The sign-in sheet opens the setup page by
      itself. Name it, pick the WiFi, tap Save; "Your pumpkin is alive!" shows until the setup WiFi goes.
      The eyes then scroll `http://pumpkin.local or <IP>`, and both open the buttons.
- [ ] "Skip WiFi for now: just use the buttons" (under Save) opens the buttons on the setup WiFi, and
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

- [ ] Doorstep: on home WiFi, take it out of range (or switch the router's 2.4 GHz off) for over 3
      minutes. "Spooky Pumpkin" appears; a phone that joins gets the buttons; checking or changing the WiFi
      asks for a BOOT press. Back in range, it rejoins the home WiFi by itself.
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
- [ ] Weak batteries: on nearly flat AAs the eyes say "New batteries please" and it doesn't keep
      restarting. On fresh ones the WiFi works. On a USB power bank, the same.
- [ ] Left running for an evening (an hour or more) with phones coming and going: still answering, and
      `pumpkin_serial.py status` shows the free memory steady.

Last: erase and flash it for posting (`flash_check`), and check the eyes scroll
"Join WiFi: Spooky Pumpkin ...".
