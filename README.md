# VSTDriver
A Windows user-mode software MIDI synthesizer driver which is capable of using any 32 or 64 bit VST Instrument

<code>Copyright (C) 2011 Chris Moeller, Brad Miller</code><br>
<code>Copyright (C) 2011, 2012 Sergey V. Mikayev</code><br>
<code>Copyright (C) 2013-2022 Christopher Snowhill (kode54)</code><br>
<code>Copyright (C) 2021 Hermann Seib</code>

## Overview
<li>The VSTi Driver Configuration</li>

![image](https://user-images.githubusercontent.com/100102043/158668700-f3b020b7-2adf-4072-aee5-592570b3e8a0.png)

<li>The loaded VSTi example</li>

![image](https://user-images.githubusercontent.com/100102043/155242979-be7ed294-53eb-4afd-98be-fad7232218ae.png)

<li>Windows registry [Computer\HKEY_CURRENT_USER\Software\VSTi Driver\Persistence] is used for VSTi settings persistance</li>

![image](https://user-images.githubusercontent.com/100102043/155243242-4c409017-0686-4382-828f-9c599fd186ef.png)

<li>ASIO support via BassAsio and driver mode selector with persistance in windows registry [Computer\HKEY_CURRENT_USER\Software\VSTi Driver\Output Driver].</li>

![image](https://user-images.githubusercontent.com/100102043/158668867-ffffecbf-453f-4bc9-9315-daff92420b4f.png)
![image](https://user-images.githubusercontent.com/100102043/158668801-645df329-d221-4d41-a71d-821c88adeb43.png)

