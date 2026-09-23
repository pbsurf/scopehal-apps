# ngscopeclient and scopehal-apps

This is the top level repository for ngscopeclient, as well as the unit tests for libscopehal.

Project website: [https://www.ngscopeclient.org](https://www.ngscopeclient.org)

## Changes in this fork

This is a fork of [ngscopeclient/scopehal-apps](https://github.com/ngscopeclient/scopehal-apps). The `lib` (scopehal) and
`doc` (scopehal-docs) submodules also carry changes, and are forks too. Compared with upstream:

**Software defined radio (ADALM-PLUTO and other AD9361/AD9363 radios)**

* Optional [libiio](https://github.com/analogdevicesinc/libiio) support (0.x API only, enabled automatically if found): an
  `iio` transport and an `iio` driver for AD936x based radios, with center frequency, bandwidth, sample
  rate and gain control. Limits come from the radio, and USB-attached radios are listed when adding an instrument.
* A simulated radio (use the path `mock:` or `mock:ad9361`) for development without hardware. It has been tested only against
  this simulation and **not yet against a real radio**.
* Receive gain mode and gain are shown in the stream browser and the channel properties dialog in place of attenuation
  for radios that have gain control. The stream browser no longer shows the offset and range of I/Q streams, which only
  affect plotting (they are still in the channel properties dialog), or an RBW field for instruments that don't have one.
* Transmit paths of radios with a DDS core are shown in the stream browser, with a gain setting (the radio's transmit
  attenuation, negated) and two tones per path with frequency and amplitude controls, and a TX LO setting shared by all
  paths, shown at full 1 Hz precision. Set a tone's amplitude to 0 to mute it.
* New Complex FFT filter for I/Q data.

**Spectrum display**

* The FFT filters have a Detector setting that controls how bins sharing a pixel column are drawn: Normal (intensity
  graded, as before), Peak (a line through the highest value in each column, like a spectrum analyzer's peak detector,
  so narrow peaks stay visible at any zoom) or Average (a line through the mean of each column; for dB data this is a
  log average).

**User interface**

* Plot interaction: dragging pans the plot when there are no cursors, Ctrl+drag zooms to a box, double click/tap on an
  axis autofits it, pinch zoom and other touch improvements, and precise axis labels and cursor positions when zoomed far in.
* Windowing and input ported from GLFW to SDL2.
* Up/Down in numeric input boxes now step the digit to the left of the cursor and apply immediately, instead of moving
  between boxes. Boxes with an Apply button only edit the text. Turn this off with `-DNUMERIC_INPUT_ARROW_STEP=OFF`.
* Manage Instruments has a Recent Instruments section with Connect and Delete buttons. Fixed reopening a recent instrument
  whose path ends in a colon (such as `mock:`).
* Numeric boxes keep the value as typed when the instrument can only set it approximately (amplitude 1% no longer reads
  back as 1.001%), and a box being stepped with Up/Down updates if the instrument then limits or rounds the value.
* Better responsiveness in event-driven (power saving) mode.
* New `--reconnect` and `--offline` options to skip the reconnect prompt when opening a session from the command line.

**Fixes and testing aids**

* Simulated function generator (`demofuncgen`) for demonstration and UI testing, saved and loaded with sessions.
* Fixed a crash in the FFT filter on an empty input waveform, and value formatting that dropped digits
  (`1.0004 GHz` was shown as `1 GHz`).
* Fixed staircase function generator shapes loading as a sine wave from a saved session.
* Fixed SDR channels losing their stream types when a session is loaded, which stopped the center frequency from being
  connected to the Complex FFT filter, and SDR transmit fields showing blank when the radio's value is zero.
* Downsample filter: the output keeps the input's X axis unit (so it stays on a frequency axis after an FFT), the
  antialiasing filter now smooths as much as intended, and a factor of zero or less reports a proper error.

**Build**

* Precompiled headers are now off by default (`DISABLE_PCH=ON`). With the Makefile generators, adding a source file to a
  target that uses them rebuilds the whole target. Use `-DDISABLE_PCH=OFF` for faster full builds.
* In-tree builds use a slimmer `scopehal.h` that no longer includes rarely used headers, so changing one of them
  rebuilds far fewer files. Out-of-tree code that includes `scopehal.h` is unaffected.
* The [mold](https://github.com/rui314/mold) linker is used automatically when installed, which needs much less memory
  than the default linker (turn off with `-DUSE_MOLD_LINKER_IF_AVAILABLE=OFF`). `instrumented-build.sh` wraps a build
  and logs rebuild and memory usage information to `build-logs/`.
* Submodule URLs work for a recursive clone of this fork.

## TODO

- test gnu radio blocks


## CI platform updates

We are no longer building with GitHub Actions and have switched to an internal CI system. This enables running tests against real GPUs from a range of vendors, and will eventually enable hardware-in-loop testing with real instruments although more infrastructure has to be deployed before that will be available.

Additionally, CI binaries are now available to the public via anonymous HTTP without requiring a GitHub login.

* Status dashboard: [https://dashboard.ngscopeclient.org/index.php?project=ngscopeclient](https://dashboard.ngscopeclient.org/index.php?project=ngscopeclient)
* Binaries: [https://dl1.ngscopeclient.org/ngscopeclient-ci/](https://dl1.ngscopeclient.org/ngscopeclient-ci/)

## Policies

* [C++ coding policy](https://github.com/azonenberg/coding-policy/blob/master/cpp-coding-policy.md)
* [Code of Conduct](https://github.com/ngscopeclient/scopehal-apps/blob/master/CODE_OF_CONDUCT.md)

## Installation

Refer to the "getting started" chapter of the User manual
* [User manual GettingStarted (HTML)](https://www.ngscopeclient.org/manual/GettingStarted.html)
* [User manual (PDF)](https://www.ngscopeclient.org/downloads/ngscopeclient-manual.pdf)

## Compilation instructions (Linux,macOS,Windows)

* [User manual Compilation (HTML)](https://www.ngscopeclient.org/manual/GettingStarted.html#compilation)

## Compiling a forked repo

If you want to contribute changes to scopehal-apps, you should make them in a
forked repo so that you can create pull request with your changes.

Following these steps:

* Fork a bunch of GitHub repos to your own GitHub account

  Right now, the build system requires that your GitHub account has cloned version of the
  VkFFT, xptools and logtools. It's best to clone the version that under the ngscopeclient
  account, this ensure that you're working with the same version as the core development
  team.

  * [ngscopeclient/VkFFT](https://github.com/ngscopeclient/VkFFT)
  * [ngscopeclient/xptools](https://github.com/ngscopeclient/xptools)
  * [ngscopeclient/logtools](https://github.com/ngscopeclient/logtools)

* Fork the `scopehal-apps` repo (this one!)  to your own account
* Clone your personal repo to your development machine

  `git clone --recursive git@github.com:<your github username>/scopehal-apps.git scopehal-apps`

* Follow the regular compilation instructions.

  If you want to create executables with debug symbols, make sure to change option `MAKE_BUILD_TYPE`
  from `Release` to `Debug` when running `cmake`.

It is possible that `cmake` errors out with the following error message:

```
CMake Error at CMakeLists.txt:82 (message):
  Unrecognized version tag 53152c7c / 53152c7c, can't create a VERSIONINFO
  from it.  Must be of format v1.2, v1.2.3, v1.2-rc3, v1.2.3-rc4
```

You can fix this by creating a dummy tag in your local repo:

`git tag v0.0.0`

## Special comments

The following standard comments are used throughout the code to indicate things that could use attention, but are
not worthy of being tracked as a GitHub issue yet.

* `//TODO`: unimplemented feature, potential optimization point, etc.
* `//FIXME`: known minor problem, temporary workaround, or something that needs to be reworked later
* `//FIXME-CXX20`: places where use of C++ 20 features would simplify the code, but nothing can be done as long as we are targeting platforms which only support C++ 17
