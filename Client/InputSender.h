 #ifndef InputSenderH
 #define InputSenderH
 
 #include <windows.h>
 #include "..\Shared\Frame.h"
 
 class RemoteClient;
 
 class InputSender {
 private:
     RemoteClient* client;
 
 public:
     explicit InputSender(RemoteClient* _client);
 
     bool MouseMove(int x, int y);
     bool MouseLeftDown(int x, int y);
     bool MouseLeftUp(int x, int y);
     bool MouseRightDown(int x, int y);
     bool MouseRightUp(int x, int y);
     bool MouseMiddleDown(int x, int y);
     bool MouseMiddleUp(int x, int y);
     bool MouseWheel(int x, int y, int delta);
 
     bool KeyDown(WORD virtualKey);
     bool KeyUp(WORD virtualKey);
 };
 
 #endif
