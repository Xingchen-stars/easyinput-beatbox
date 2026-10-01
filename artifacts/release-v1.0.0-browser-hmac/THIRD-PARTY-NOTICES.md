# Distribution notices for v1.0.0

The project is a personal course modification of Zhaohan-Wang/easyinput-beatbox. Its MIT license and original copyright notice are retained in `LICENSE`.

## Beatbox firmware package

- ESP-IDF 5.5.5 by Espressif: Apache License 2.0, reproduced as `LICENSE-ESP-IDF.txt`. ESP-IDF also contains third-party components with their own notices in its upstream source; this package does not redistribute or relicense those source trees.
- espressif/led_strip 2.5.5: Apache License 2.0, reproduced as `LICENSE-led-strip.txt`.
- Drum samples from fluid-music/open-drums, TR-707 pack: the upstream pack states the samples are public domain. Original provenance and conversion details are preserved in `SAMPLE-PROVENANCE.md` in the firmware package and `assets/samples/tr707/README.md` in the source.
- Added spoken prompts are generated project assets; they are not recordings of a named person's voice.

## Web package

- Drum icons: oclero/qlementine-icons, copyright (c) 2023 Olivier Cléro, MIT. The full notice is included as `LICENSE-qlementine-icons.txt` in the ZIP and `app/src/assets/icons/drums/LICENSE` in source.
- General interface icons: Lucide, ISC license. The package's full notice is included as `LICENSE-lucide.txt` in the web ZIP; the build dependency is pinned by `pnpm-lock.yaml`.

## EasyInput keyboard integration source

`easyinput-factory/` is a separate EasyInput source snapshot. It retains PolyForm Noncommercial License 1.0.0, its original copyright notices and `THIRD_PARTY_NOTICES.md`. The root Beatbox MIT license does not override that subtree's license. Do not use the inclusion of this snapshot as permission for commercial use. The snapshot is not claimed to be the complete upstream hardware/design distribution or newly rebuilt keyboard firmware in this release.
