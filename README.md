# VSTDriver
A Windows user-mode software MIDI synthesizer driver which is capable of using any 32 or 64 bit VST Instrument

`Copyright (C) 2011 Chris Moeller, Brad Miller`  
`Copyright (C) 2011, 2012 Sergey V. Mikayev`  
`Copyright (C) 2013-2022 Christopher Snowhill (kode54)`  
`Copyright (C) 2021 Hermann Seib`

## Overview
* The VSTi Driver Configuration

   ![image](https://user-images.githubusercontent.com/100102043/158668700-f3b020b7-2adf-4072-aee5-592570b3e8a0.png)

* The driver's VST host with the loaded VSTi.

   ![image](https://user-images.githubusercontent.com/100102043/159137233-3626111c-b604-4889-934a-3d8a35c6e149.png)
   <details>
   <summary>Coming soon</summary>
   
   * *The VST host can be visible in your DAW.*
   * *The VST host can be minimized.*
   * *The VSTi Setup works in real time.*
   
   </details>
   

* VSTi settings are stored in Windows registry

   ![image](https://user-images.githubusercontent.com/100102043/159165630-87c02d79-035c-42f5-a779-88079d40d3e7.png)

   ![image](https://user-images.githubusercontent.com/100102043/155243242-4c409017-0686-4382-828f-9c599fd186ef.png)

* Output drivers & driver mode selectors for ASIO, WASAPI Shared or WASAPI Exclusive.

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
