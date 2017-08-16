## Aim Acceleration Explorer

In fps games, aim acceleration is related to how a physical movement of the input device, such as a gamepad analog stick,
is translated into changes in the shooter's look direction. This software is created to measure aim acceleration and such
metrics as inner/outer dead zone, sensitivity remapping curve in any first person game like Call of Duty or Battlefield.


## How it works? (Technical details)

The software automatically finds the memory addresses that store the yaw/pitch values and then displays the values stored in
founded addresses and the corresponding analog gamepad stick values for each game frame.


The whole process is divided into two steps:

- **MemoryScan.**
  At this stage, we scan the whole game memory and look for bytes that change when a player a rotating. Then we examine and find
  the valid ranges for data stored in corresponding memory. This gives us enough information to recognize yaw/pitch values stored in memory.

- **Visualization.**
  For create a great visualization, we need to know about the timing of every game frame. That would help us to match the values
  read from memory and the game frames. For this purposes, we use a non-intrusive D3D hook using ETW API. The yaw and pitch read
  from the game memory, the gamepad stick values and the corresponding game frames are then drawing in real-time as charts.


The final result looks something like this

![Example screenshot](https://raw.githubusercontent.com/SergeyMakeev/AimExplorer/master/screenshot.png)

I hope this helps you to understand how aim acceleration works in modern fps games.
