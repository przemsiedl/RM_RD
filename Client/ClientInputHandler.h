 #ifndef ClientInputHandlerH
 #define ClientInputHandlerH
 
 #include <windows.h>
 
 class RemoteClient;
 class InputSender;
 
 class ClientInputHandler {
 private:
     RemoteClient* client;
     InputSender* sender;
 
     void LocalToRemoteCoords(HWND hwnd, int localX, int localY, int& remoteX, int& remoteY) const;
 
 public:
     ClientInputHandler(RemoteClient* _client, InputSender* _sender);
 
     bool HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
 };
 
 #endif
