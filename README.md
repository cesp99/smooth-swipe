# Smooth Swipe

A KWin (Plasma 6) desktop-switching effect that makes the 3-finger swipe between
virtual desktops feel like macOS.

It is a fork of KWin's built-in **Slide** effect with the physics reworked:

- **One continuous spring simulation** — while your fingers are down, the view
  follows them through a very stiff spring advanced every frame, which filters
  touchpad jitter and the misalignment between input events and display refresh
  (stock Slide hard-locks to raw gesture events). On release, the spring simply
  retargets to the destination desktop, keeping its position *and* velocity —
  so your fingers' momentum carries into the release animation instead of it
  starting from a dead stop. Grabbing mid-animation is seamless the same way.
- **Tunable spring physics** — spring stiffness and damping ratio are exposed in
  the effect's settings (stock hardcodes an overdamped 300 / 1.1 spring).
  Defaults are tuned to feel like macOS Spaces: stiffer and critically damped,
  with a subtle natural overshoot on fast flicks.
- **Momentum transfer amount** is configurable (0–200%).

## Requirements

- Plasma 6 / KWin 6.7.x (binary effects must match the installed KWin version)
- KWin headers (on Arch they ship with the `kwin` package)
- cmake, extra-cmake-modules, a C++ compiler, Qt 6 and KF6 dev packages

## Build & install

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

Then either restart the session or reload KWin, and enable the effect:

- System Settings → **Desktop Effects** → *Virtual Desktop Switching Animation*
  → select **Smooth Swipe** (it replaces Slide — they are mutually exclusive).
- Click the settings button on the effect to tune stiffness, damping and
  momentum transfer.

After changing settings while the effect is running, KWin picks them up
immediately (the config module tells KWin to reconfigure over DBus).

## Rebuilding after a KWin update

The effect plugin embeds the KWin plugin ABI version (`6.7.4`). After a KWin
upgrade, rebuild and reinstall or KWin will refuse to load the plugin.

## License

GPL-2.0-or-later. Derived from KWin's Slide effect
(© Lubos Lunak, Lucas Murray, Vlad Zahorodnii).
