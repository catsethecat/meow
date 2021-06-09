Very unfinished chat program, but it is technically usable in its current state  
  
Features:  
-voice chat with lossless audio  
-fully configurable audio parameters (channels, bitdepth, samplerate)  
-global hotkeys that can be configured for mic toggle, push to talk, push to mute etc  
-optional postprocessing effects such as changing volume of other users and a basic noisegate  
-basic text chat  
-supports multiple users per chatroom, chatroom is determined by the key used  
-all important data is sent peer to peer and end-to-end encrypted with aes256, the server is used for peer discovery only  
-can recover from some packet drops  
-minimalist software, no third party libraries, uses windows' native built in APIs directly  
  
![screenshot1](https://cdn.discordapp.com/attachments/852088618594992159/852091479790190603/unknown.png)  
  
To build the application using the included build.bat you need to have Visual Studio installed.  
First open build.bat in a text editor and make sure the vcvarsall path inside is correct.  
Then simply run build.bat by double clicking on it.  
  
The server can be built on linux aswell, simply "cc server.c"  
