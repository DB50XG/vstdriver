# VSTDriver
A Windows user-mode software MIDI synthesizer driver which is capable of using any 32 or 64 bit VST Instrument

`Copyright (C) 2011 Chris Moeller, Brad Miller`  
`Copyright (C) 2011, 2012 Sergey V. Mikayev`  
`Copyright (C) 2013-2022 Christopher Snowhill (kode54)`  
`Copyright (C) 2021 Hermann Seib`

## Overview
* The VSTi Driver Configuration

   ![image](https://user-images.githubusercontent.com/100102043/158668700-f3b020b7-2adf-4072-aee5-592570b3e8a0.png)

* The loaded VSTi example

   ![image](https://user-images.githubusercontent.com/100102043/155242979-be7ed294-53eb-4afd-98be-fad7232218ae.png)

* Windows registry [Computer\HKEY_CURRENT_USER\Software\VSTi Driver\Persistence] is used for VSTi settings persistence. Each VSTi settings are stored separately.

   ![image](https://user-images.githubusercontent.com/100102043/155243242-4c409017-0686-4382-828f-9c599fd186ef.png)

* ASIO support via BassAsio and driver mode selector with persistence in windows registry [Computer\HKEY_CURRENT_USER\Software\VSTi Driver\Output Driver].

   ![image](https://user-images.githubusercontent.com/100102043/158668867-ffffecbf-453f-4bc9-9315-daff92420b4f.png)
   ![image](https://user-images.githubusercontent.com/100102043/158668801-645df329-d221-4d41-a71d-821c88adeb43.png)

## Build
* <a href="https://visualstudio.microsoft.com/vs/">Visual Studio 2022</a>
* <a href="https://nsis.sourceforge.io/Download">NSIS</a>
* <a href="https://nsis.sourceforge.io/LockedList_plug-in">LockedList plug-in</a>

## Debug
* Install the VST driver.
* Enable the post-build event of the project that you want to debug and rebuild it in debug mode.
* Use a DAW like <a href="https://www.bandlab.com/products/cakewalk">Cakewalk</a> and attach to it.
