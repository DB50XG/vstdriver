# VSTDriver
A Windows user-mode software MIDI synthesizer driver which is capable of using any 32 or 64 bit VST Instrument

<code>Copyright (C) 2011 Chris Moeller, Brad Miller</code><br>
<code>Copyright (C) 2011, 2012 Sergey V. Mikayev</code><br>
<code>Copyright (C) 2013-2022 Christopher Snowhill (kode54)</code>

<details>
 <summary>History</summary>
  <i>
   I discovered this driver in May 2020.
   
   At the end of 2020 I decided to add ASIO support and fix the broken VSTi settings persistence.
   
   The VSTi settings persistance was fixed by saving the chunks to the Windows registry.
   
   At the end of July 2021 the only missing fix was the installer which I wanted to take from another project and then make it public.
   
   Unfortunately at this point I lost the code completely.
   
   I'm trying to get to the same refactoring point by using 2-3 months earlier version of the project.
   
   For now, you can use the installer from <a href="https://github.com/Arakula/vstdriver/releases/tag/v1.0.0-alpha3">Arakula</a> and replace the files manually.
  </i>
  
</details>

## Overview
<li>The VSTi Driver Configuration</li>

![image](https://user-images.githubusercontent.com/100102043/155243100-daf26d14-cc68-4756-8f61-bb25949199d7.png)

<li>The loaded VSTi example</li>

![image](https://user-images.githubusercontent.com/100102043/155242979-be7ed294-53eb-4afd-98be-fad7232218ae.png)

<li>Windows registry is used for VSTi settings persistance</li>

![image](https://user-images.githubusercontent.com/100102043/155243242-4c409017-0686-4382-828f-9c599fd186ef.png)

<li>ASIO support via BassAsio</li>

## To-Do
<li>Add ASIO / WASAPI selector in VSTi Driver Configuration</li>
<li>Installer</li>
