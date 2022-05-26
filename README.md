## Features
- Voice chat with lossless audio
- Encrypted peer-to-peer communication (the server is used for peer discovery only)
- Fully configurable audio parameters (channels, bitdepth, samplerate)
- Global hotkeys that can be configured for mic toggle, push to talk, push to mute etc
- Optional postprocessing effects such as changing volume of other users and a basic noisegate
- Basic text chat
- Supports multiple users per channel, channel is determined by the server and key used
- Can recover from some packet drops
- Minimalist software, no third party libraries, uses windows' native built in APIs directly
- The whole application is 23 KB
  
![image](https://user-images.githubusercontent.com/45233053/170481636-2943db7b-9253-496c-8fe2-19f03151b069.png)

### Setup
- Download the application from [Releases](https://github.com/catsethecat/meow/releases) or build it from source code
- Run it and use the /cfg command to open the configuration file in a text editor.
- Edit the Channels section, you can add multiple channels, one per line, in this format:  
  ```ChannelName = ServerAddress:ServerPort:EncryptionKey```  
  EncryptionKey should be set to 32 random bytes in hexadecimal format (64 characters A-F 0-9)
- Set DefaultChannel to the name of the channel which you want to use when starting the application
- Restart the application
- You should now be able to communicate with other users who are using the same channel (same server and key)

