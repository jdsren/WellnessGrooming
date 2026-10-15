# Wellness Grooming (SKSE plugin, no ESP, no scripts)

An add-on for Wellness / Last Seed / Stress and Fear on Skyrim SE 1.5.97 (also works on AE).
Everything runs inside one DLL. It does nothing until you press **Start** in the SKSE Menu Framework menu.

## What it does
* **Bathroom.** Drinks fill the bladder, food reaches the bowel about 12 game hours later. Reminders, auto-relief when desperate, optional accidents.
  Default numbers: bladder 450 ml, about 5 pees and 1 bowel movement per day with 8 drinks and 3 meals (tested in `test/model_test.cpp`).
* **Intake.** Drinks and meals are read from the Last Seed item lists (water, milk, ale, wine, spirits, food tiers); other food is guessed from its name. Every size is adjustable.
  Thirst in Last Seed lowers urine production, alcohol raises it.
* **Hair and beard.** Choose up to 18 styles each, in order (vanilla, Beards of Power, any hair mod: the menu lists every hair/beard part in your load order that fits your race and sex).
  Time makes the style step to the next one; hunger and stress slow it. Trim with the Dynamic NPC Hairstyles scissors (or any item with scissors/shears/razor in its name) to go back 2 styles; Shave goes to slot 1.
  A style set by a barber or RaceMenu is noticed: if it is on your list growth continues from there, otherwise growth pauses.
* **Teeth.** Plaque builds over 36 hours; brush from the menu or a hotkey.
* **Stress and Fear.** Being desperate raises stress, relief and a haircut lower it, stress slows growth.
* **Animations.** Uses the Alive Peeing animation events (standing, one-hand, stooping, squatting, kneeling) and the Nature Calls pose. If they are missing it crouches with the game's own animation. Option: black screen fade.
* **Screen bars** (bladder, bowel, teeth, hair, beard) and optional hotkeys.

## Build (GitHub, no Visual Studio)
1. New GitHub repository, upload everything in this folder (upload `src`, `src/third_party`, `test` as folders, plus `CMakeLists.txt`, `vcpkg.json`, `README.md`).
2. Create `.github/workflows/build.yml` by hand ("Add file > Create new file", type that path) and paste in the text of `build.yml.txt`.
3. Open the **Actions** tab, wait for the green tick, open the run, download **WellnessGrooming** at the bottom.
   If the run is red, copy the red error lines and send them back.

## Install
1. Install **SKSE Menu Framework** (needed for the menu), Address Library, SKSE64.
2. Put `SKSE/Plugins/WellnessGrooming.dll` in a new mod folder.
3. Keep the animation files of **Alive Peeing** (the `AliveAnimationPack`) and, optionally, **Nature Calls**, then run Pandora/FNIS so the events exist.
4. **Remove or disable the ChatGPT "PlayerGrooming" DLL**; two mods changing hair parts will fight each other.
5. Start the game, open the SKSE Menu Framework menu (default key is shown in its own MCM), pick **Wellness Grooming**.
   Choose your styles on the Hair and Beard pages, then press **Start** on the first page.
6. On first run `Data/SKSE/Plugins/WellnessGrooming.ini` is written with every option (and the linked record ids). Under Mod Organizer it ends up in Overwrite.

## Log
`Documents/My Games/Skyrim Special Edition/SKSE/WellnessGrooming.log`

## Honest limits
* No new animation files are included; I can not author .hkx files. The pretty poses come from the original mods.
* Not run in a real game: it compiles against CommonLibSSE-NG and the logic is unit-tested, but the first run may show small things to fix. Send the log if something misbehaves.
* Stress and Fear is assumed to use 0-100 for `Stress_Total`. Last Seed hunger/thirst levels are assumed 0-5 (change "Highest Last Seed level" if not).
* Light/ESL plugins are looked up by the id stored in the file; if a linked record shows "missing" on the Linked Mods page, edit its id there.
