# Changelog

## v0.6
* Fixed the game freezing when certain Pokémon appeared (seen with Alolan Grimer). Each Pokémon now
  gets as much memory as the game itself gives these models.
* New performance option: **Pokemon display: 2D icons**. Pokémon are shown as their PC box icons
  instead of 3D models: no model loading and no extra memory. Icons face the way the Pokémon walks,
  hop while walking, get smaller with distance and shinies sparkle. Needs `owe_icons.bin`, built
  from your own copy of the game with `tools/make_icons.py`.

## v0.5
* Pokémon cry more often and more naturally: when they pop out, when they stop after a walk or
  look around, plus the odd random call. Cries never overlap, have a short gap between them and
  vary slightly in pitch.
* Pokémon keep appearing while you stand still in the grass.
* Wandering Pokémon no longer walk into you and start a battle by themselves.

## v0.4
* Pokémon cries.
* Up to 5 Pokémon in big grass patches (menu option to limit it to 3).
* Pokémon are shown slightly larger, and very small species get an extra boost.
* Memory is handed back to the game while no Pokémon are out.

## v0.3
* Fixed the game freezing after a few battles.
* Pokémon walk around in the grass instead of standing still.

## v0.2
* Fixed the game freezing as soon as you enter tall grass when the emulator runs in Old 3DS mode.
  The number of Pokémon shown at once now adapts to the available memory.

## v0.1
* First release.
