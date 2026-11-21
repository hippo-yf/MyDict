# Qt edition third-party notices

MyDict application source is MIT licensed (see LICENSE). Authors: Yance Feng & Codex.
The C++ edition dynamically links Qt Core, Gui, Widgets, Network, Sql, Concurrent,
Svg, and the platform/SQLite/TLS plugins. Qt is separately licensed; it is not
covered by MyDict's MIT license. The open-source Qt components used here are
distributed under LGPL v3 (or their stated alternative license). QtTest is used
only for testing and is not included as an application dependency.

Users may replace compatible dynamically linked Qt libraries and debug their
modifications. This application does not prohibit reverse engineering for that
purpose. No static Qt linking is used. Build instructions and the complete MyDict
source are supplied in this repository.

- Qt licenses and source: https://doc.qt.io/qt-6/licensing.html
- Matching Qt source archives: https://download.qt.io/official_releases/qt/
- Conda Qt build recipes and patches: https://github.com/conda-forge/qt-main-feedstock
- LGPL v3: https://www.gnu.org/licenses/lgpl-3.0.html
- GPL v3 (incorporated by LGPL v3): https://www.gnu.org/licenses/gpl-3.0.html

Portable packages include dependency version/build receipts and the license files
available in the Conda package cache under third-party-licenses. Qt's shared runtime
may depend on OpenSSL, SQLite, ICU, HarfBuzz, FreeType, libpng, zlib, zstd and other
platform libraries, each under its own license. The receipts identify the actual
package set; package and platform dependency sets differ.

The four public CaskaydiaCove Nerd Font files in fonts/ retain their included
CASCADIA-OFL.txt and NERD-FONTS-LICENSE.txt licenses. fonts/local/ is private and
never installed or packaged by the Qt CMake manifest. Font rendering falls back to
system fonts when configured fonts are absent.

Noto Sans SC is additionally included for Chinese glyph coverage on Linux and
other systems without a Chinese font. Its SIL Open Font License is supplied as
fonts/NOTO-SANS-SC-OFL.txt. Source: https://github.com/google/fonts/tree/main/ofl/notosanssc.

Dictionary content retains its source licensing: Free Dictionary API records
include source/license references, and Wiktionary text is CC BY-SA. DeepSeek,
MyMemory and LibreTranslate are user-configured remote services; application
licensing does not replace their service terms or dictionary attribution.
