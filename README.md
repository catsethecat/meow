## Features
- Voice chat with lossless audio
- Encrypted peer-to-peer communication (the server is used for peer discovery only)
- Configurable audio parameters (mono/stereo, bitdepth, samplerate)
- Global hotkeys that can be configured for mic toggle, push to talk, push to mute etc
- Basic noisegate feature
- Basic text chat
- Supports up to 9 users per channel, channel is determined by the combination of server, channel id and key used
- Can recover from some packet drops
- Minimalist software, the whole application is 24 KB
  
![image](https://user-images.githubusercontent.com/45233053/170481636-2943db7b-9253-496c-8fe2-19f03151b069.png)

## Setup
- Download the application from [Releases](https://github.com/catsethecat/meow/releases) or build it from source code
- Run it and use the /cfg command to open the configuration file in a text editor.
- Edit the Channels section, you can add multiple channels, one per line.   
  Format: ```ChannelName = ServerAddress:ServerPort:ChannelID:EncryptionKey```  
  Example: ```Cats = catse.net:64771:c660c8672f6105b1:af9afecfb6e774f7c7ac9d48da63cf64b69c703c12508e34f19f96dddfc6e960```  
  ChannelID should be set to 8 random bytes, EncryptionKey to 32 random bytes, both in hexadecimal format. 
  A secure method should be used to share channel information with friends who you want to use the application with.
- Set DefaultChannel to the name of the channel which you want to use when starting the application
- Restart the application
- You should now be able to communicate with other users who are using the same channel (same server/id/key combo)
#### Hosting a dedicated server
- If you want to host your own server, simply forward the appropriate port and run the server software
- The server can be compiled on linux. example: ```gcc server.c -o meow_server```

## Notes
- A public server is available at ```catse.net:64771``` if you don't want to host your own
- A fast, reliable internet connection is required. The application is very sensitive to packet drops and latency. You can try mitigating audio cutouts and glitches by increasing the target delay in the config.
- 16 bit 44100 Hz Mono (default config) audio requires a network bandwidth of 0.7 Mbps per user. There is currently no data compression.
