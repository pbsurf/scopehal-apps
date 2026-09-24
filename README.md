# ngscopeclient and scopehal-apps

This is a fork of [ngscopeclient/scopehal-apps](https://github.com/ngscopeclient/scopehal-apps).  Due to the use of AI here, forks of this repo should not submit pull requests to the original ngscopeclient.

**New features**

* SDR support for AD936x radios (ADALM-PLUTO etc.) via optional [libiio](https://github.com/analogdevicesinc/libiio): RX frequency, bandwidth, sample rate and gain
* SDR transmit: DDS tones with frequency/amplitude, TX gain and TX LO in the stream browser
* SDR sweep: spans wider than one capture sweep the LO, and the new Spectrum Stitch filter joins the captures into one spectrum
* Complex FFT filter for I/Q data
* Detector setting (Normal/Peak/Average) for drawing FFT and Spectrum Stitch
* Plot interaction: drag to pan, Ctrl+drag box zoom, double click an axis to autofit, pinch zoom and touch improvements
* Windowing and input ported from GLFW to SDL2, enabling touch input support
* Up/Down in numeric boxes steps the digit left of the cursor
* Recent Instruments section in Manage Instruments

**Minor features and bug fixes**

* Precise axis labels and cursor positions when zoomed far in
* Receive gain replaces attenuation in the stream browser and channel properties for radios with gain control
* Numeric boxes keep the typed value when the instrument rounds it, and update while being stepped if it changes
* A bare number typed in a numeric box keeps the unit and prefix shown before ("2" in a "1 MHz" box is 2 MHz)
* `--reconnect` and `--offline` options to skip the reconnect prompt when opening a session from the command line
* Better responsiveness in event-driven (power saving) mode
* Simulated function generator (demofuncgen) for demos and UI testing
* Peak markers show frequency to a tenth of a bin, and FWHM as an upper bound at the two-bin minimum
* Fixed FFT peak detection ignoring the Peak Window (it reported every local maximum as a peak)
* Fixed unit parsing that read a unit's first letter as a prefix ("5 mV" in millivolts, "2 m" in meters)
* Fixed FFT crash on an empty input, and value formatting dropping digits ("1.0004 GHz" shown as "1 GHz")
* Fixed staircase function generator shapes loading as sine, and SDR channels losing stream types on session load
* Fixed empty saved font paths overriding the default fonts
* Build: slimmer `scopehal.h` for in-tree builds, so changing a rarely used header rebuilds far fewer files
* Build: [mold](https://github.com/rui314/mold) linker used when installed

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
