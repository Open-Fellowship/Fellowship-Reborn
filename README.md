# Fellowship Reborn

<img width="1280" height="596" alt="Lord of the Rings The Fellowship of the Rings" src="https://github.com/user-attachments/assets/2f53649a-7a97-4e86-9a7a-4add2c5aabd8" />


This is a reverse engineering, preservation and improvement project for the PC version of The Lord of the Rings: The Fellowship of the Ring.

## Scope

The aim of this project can be found summarised below: 
- To fix the game up to a standard where it works on modern hardware/systems and OS's (Windows, Linux), add modern improvements such as modern Resolution/Aspect Ratio support, corrected HUD, higher FPS support, higher FOV support, better windowed mode support and much more.
- Include mod tools for use in Blender via plugins where users can create their own modifications to the game and do much more with characters, props, environments etc.
- Restore the content from the various ports back into the PC port to make Fellowship a complete game as it was meant to be.
- Restore the developer menu for the game and add in some new features.

## Requirements

The user must have their Lord of the Rings: The Fellowship of the Ring game patched to the 1.1 version of the game in order for Fellowship Reborn to work correctly. If you run into issues with this please join the Lord of the Rings: The Fellowship of the Ring Modding Discord server.

## Install

Go to releases and download the latest Fellowship Reborn release. Extract all the contents into the main Fellowship of the Rings install location. This should consist of:
- Plugins folder
- dinput8.dll
- fellowship_reborn.ini
- levellist.txt
- fotr_riot_importer.zip

## Use

Once you have everything installed you can edit any settings you wish to change within the fellowship_reborn.ini file.
- When in the game the user can change their Resolution within the video options menu to their desired setting. 
- The user can now select any level within the game by clicking the 'New Game' option on the Main Menu. This will allow the user to load into any level that they wish to right away or just load into the starting level 'Hobbiton' like normal.
- When in gameplay the user can press the ' key to load up the dev menu. This will allow the user to have access to the following tabs:
- Camera
- Engine Flags
- Messages
- Fellowship Reborn

- The 'Camera' tab will allow the user to edit their FOV, which they can either set it to automatic where it is set for the specific resolution or use the slider to change it to what they desire.

- The 'Engine Flags' tab will allow the user to turn on and off the various commands that were used in the games original developer menu.

- The 'Messages' tab will allow the user to see engine messages (only developers will use this).

- Fellowship Reborn tab contains options added into the game by the Fellowship Reborn team. These include player size to allow the user to change this with sliders for height and width. Also frame rate where the user can use set FPS of 30, 60, 120 and 144 buttons or use the slider to choose any FPS option they wish to use.

## Modding Tools

This is an ongoing development atm.

Currently once the user has downloaded the fotr_riot_importer.zip they will need to install Blender 5.0 or newer and then drag and drop fotr_riot_importer.zip into Blender window. The user can then open up models, characters and maps from the game to edit.

## Cut Content

Currently this is an in progress area and will be done further over time but will aim to restore the missing content from the game and its various ports into the PC version. More will be mentioned about all of this at a later date.

## GOG

If you are interested in potentially seeing this game easily available to purchase and use today then go and vote on the games GOG Dreamlist to help make this become a reality, you can vote for the game here and write a message about the game and this project if you wish – https://www.gog.com/dreamlist/game/the-lord-of-the-rings-the-fellowship-of-the-ring-2002

## Issues/Problems

If you have any issues then please go to discord for help - https://discord.gg/pt4Chk37sv 

## Credits

brought to you by Fix Enhancers
https://fixenhancers.wixsite.com/fix-enhancers

Team members
Chip and JokerAlex21 

## Licence

MIT, with one exception. See `LICENSE`.

The Blender extension in `tools/blender/fotr_importer/` is **GPL-3.0-or-later**, declared in
its own `blender_manifest.toml`. Blender's extensions platform requires it of anything that
imports `bpy`, so the add-on cannot carry the same licence as the rest of the tree. Nothing
outside that directory is affected, and nothing in the runtime links against it.

Not affiliated with or endorsed by any rights holder; all trademarks belong to their
respective owners.
