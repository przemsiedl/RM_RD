 #include "ClientInputHandler.h"
 #include "RemoteClient.h"
 #include "InputSender.h"
 
 ClientInputHandler::ClientInputHandler(RemoteClient* _client, InputSender* _sender)
     : client(_client), sender(_sender) {
 }
 
 void ClientInputHandler::LocalToRemoteCoords(HWND hwnd, int localX, int localY, int& remoteX, int& remoteY) const {
     if (!client || client->remoteScreenWidth == 0 || client->remoteScreenHeight == 0) {
         remoteX = localX;
         remoteY = localY;
         return;
     }
 
     RECT clientRect;
     GetClientRect(hwnd, &clientRect);
 
     int clientWidth = clientRect.right - clientRect.left;
     int clientHeight = clientRect.bottom - clientRect.top;
 
     remoteX = (localX * client->remoteScreenWidth) / clientWidth;
     remoteY = (localY * client->remoteScreenHeight) / clientHeight;
 }
 
 bool ClientInputHandler::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
     if (!client || !sender) return false;
 
     switch (message) {
         case WM_LBUTTONDOWN:
         case WM_LBUTTONUP:
         case WM_RBUTTONDOWN:
         case WM_RBUTTONUP:
         case WM_MBUTTONDOWN:
         case WM_MBUTTONUP:
         case WM_MOUSEMOVE: {
             int localX = LOWORD(lParam);
             int localY = HIWORD(lParam);
             int remoteX, remoteY;
             LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
 
             switch (message) {
                 case WM_LBUTTONDOWN:
                     return sender->MouseLeftDown(remoteX, remoteY);
                 case WM_LBUTTONUP:
                     return sender->MouseLeftUp(remoteX, remoteY);
                 case WM_RBUTTONDOWN:
                     return sender->MouseRightDown(remoteX, remoteY);
                 case WM_RBUTTONUP:
                     return sender->MouseRightUp(remoteX, remoteY);
                 case WM_MBUTTONDOWN:
                     return sender->MouseMiddleDown(remoteX, remoteY);
                 case WM_MBUTTONUP:
                     return sender->MouseMiddleUp(remoteX, remoteY);
                 case WM_MOUSEMOVE:
                     return sender->MouseMove(remoteX, remoteY);
             }
             break;
         }
 
         case WM_SYSKEYDOWN:
         case WM_KEYDOWN:
             return sender->KeyDown((WORD)wParam);
 
         case WM_SYSKEYUP:
         case WM_KEYUP:
             return sender->KeyUp((WORD)wParam);
     }
 
     return false;
 }
