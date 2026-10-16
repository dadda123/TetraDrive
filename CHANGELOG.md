# Changelog

Add a new `## [x.y.z] - YYYY-MM-DD` section at the top for every release. The topmost version is the plug-in version.

## [1.1.0] - 2026-10-15
- Thanks to Steve Duda (Xfer Records) for contributing all of the 1.1.0 features below.
- Added: Oversampling (Off, 2x, 4x, 8x) in the Global panel. The waveshapers run at the higher rate; crossovers and the sound of each shape are unchanged. Latency is reported to the host.
- Sessions saved with 1.0.0 load with oversampling off, so they recall exactly as before. New instances default to 2x.
- Oversampling is not stored in presets.
- Added: Crossover mode (Classic, Flat) in the Global panel. Classic is the original Butterworth band-pass split, with its characteristic ripple where bands meet. Flat uses Linkwitz-Riley filters so the bands cross at -6 dB and sum back to a flat response; the low and high cuts are applied once ahead of the split. The spectrum's band curves follow the selected mode. Older sessions load as Classic; the mode is stored in presets.
- Added: a saturation lamp beside every Drive knob. It measures how far the shaped band differs from the clean band and glows accordingly: dark when transparent, band colour when audibly saturating, white when the band is mostly distortion. The pucks on the spectrum glow from the same measurement.
- Added: metering. Each band strip's colour bar shows the band's output level, and the Global panel has stereo input and output peak meters with hold and clip indication.
- New logo: a four-tab blotter printed with the four-ring core of a certain molecule, drawn procedurally as vector paths so it is sharp at any size.
- On the About page the print is alive: the molecule draws itself in from the ring junction outwards each time the page opens, then keeps flexing in gentle pseudo-3D while the tie-dye rings drift outwards.
- Added: the analyzer can show the output spectrum as well as the input. The Analyzer button now offers Off, Input, Output and Input + Output; with both, the output is drawn bright over a muted input so the harmonics each band adds are visible.

## [1.0.0] - 2026-10-14
- Initial release.
