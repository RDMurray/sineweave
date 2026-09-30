# Third-party notices

Sineweave project code is AGPL-3.0-only; see LICENSE. Distribute corresponding
source, exact dependency versions, this file, and the licenses directory with
binary releases. This project uses JUCE under AGPLv3, not its commercial EULA.

* **JUCE 8.0.15** (`91ad83ae34a81e0833b1a2b0866f54846370ae53`), Copyright
  Raw Material Software Limited. AGPLv3/commercial dual licence, AGPLv3 route used.
  Original licence index: `licenses/JUCE-LICENSE.md`.
* **Loris** (`eccd42ac847b08769643b0d0c36442ef0fd27f38`), Copyright
  1999–2026 Kelly Fitz and Lippold Haken. GPLv2 or later. GPLv3-compatible
  combination with AGPLv3; upstream licensing remains unchanged. Original
  GPL text: `licenses/Loris-GPL-2.0.txt`.
* **VST3 SDK** bundled with this JUCE version, Copyright 2025 Steinberg Media
  Technologies GmbH. MIT; see `licenses/VST3-MIT.txt`.
* Loris's bundled **Ooura FFT** retains its original notices in upstream
  `src/fftsg.c`. FFTW is not linked.
* JUCE includes FLAC and Ogg Vorbis (BSD), JPEG (IJG), PNG/zlib (zlib), HarfBuzz
  (MIT), and SheenBidi (Apache 2.0). Their original licence texts/notices are
  preserved in `licenses/juce-dependencies/` and indexed by JUCE-LICENSE.md.
  Disabled modules such as the browser, JavaScript, AAX, LV2 plugin format, and
  ASIO are not used by this Windows VST3 build.

Source: https://github.com/juce-framework/JUCE and https://github.com/kellyfitz/loris.
The dependency fetch pins, static Loris integration, and optional CPU-dispatched
AVX2 kernel are recorded in CMake and docs/technical-plan.md.
