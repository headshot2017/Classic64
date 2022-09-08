# Classic64
A plugin for [ClassiCube](https://classicube.net) which uses libsm64 to insert a playable Mario from Super Mario 64 into the game.

![Hey stinky](screenshot.png)

This is still a work in progress! There is no download available yet. However, you can compile from source code and play around with it.

[libsm64 audio fork by ckosmic](https://github.com/ckosmic/libsm64/tree/audio)<br/>
This project was not created by, or is related with the developers of the Minecraft mod "Retro64".<br/>
[ClassiCube](https://github.com/UnknownShadow200/ClassiCube) is a custom Minecraft Classic client written in C.

## Compiling the plugin
Download the [MSYS2](https://msys2.org/#installation) development environment, launch MSYS2 MinGW x64 and install gcc, g++ and make.
Finally, run `make`.

A folder named "dist" will be created and inside will sit the plugin dll file "Classic64.dll".
Copy this to your ClassiCube plugins folder.

## Running the plugin
**You are required to supply a dumped copy of your Super Mario 64 US ROM, placed inside the plugins folder, with the filename "sm64.us.z64".
Otherwise the plugin will refuse to work.**

This plugin only works on 64-bit systems, Windows and Linux. macOS has not been tested.

## How to play
To turn into Mario, run the command `/model mario64`. This will let everyone on the server using this plugin know that you're playing as Mario, and will display the model on their end.

If you are on a server with hacks enabled (flying, speed, etc) but the "/model" command is not allowed, you can run the client-side command `/client mario64 force`, at the cost that nobody will see your Mario model.

## Running client-side commands with MessageBlocks or Bots
If you're making a map intended for Mario, you can alter certain aspects of the plugin through your map by using MessageBlocks, or Bots! (Only on MCGalaxy servers)

The format is `&0!mario64`, followed by one of the client-side commands listed below.

Examples:<br/>
If you want to play music ID 27 (Correct Solution) when breaking a white wool block: `/mb white &0!mario64 music 7`<br/>
If you want to instantly kill Mario when a bot kills the player: `/bot deathmessage <bot-name> &0!mario64 kill`

## Client-side commands
All client-side commands start with `/client mario64`

* `/client mario64 music <ID 0-33>`<br/>
Plays music from Super Mario 64.<br/>
To stop music, use music ID 0.

* `/client mario64 cap <option>`<br/>
Change Mario's cap.<br/>
You can choose one of these options: off, on, wing, metal

* `/client mario64 kill`<br/>
Kills Mario instantly.

* `/client mario64 force`<br/>
Force-switch between Mario and the default model.<br/>
Useful if the /model command is disallowed but hacks are enabled.

* `/client mario64 settings <option> [value]`<br/>
Change plugin settings.<br/>
To show all settings, use `/client mario64 settings`<br/>
To show information about a setting (what it does, current value, etc), leave out the value argument: `/client mario64 settings <option>`<br/>
Settings are saved to a file named "Classic64.cfg" in your ClassiCube folder.
