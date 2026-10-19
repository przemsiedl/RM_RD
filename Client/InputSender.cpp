 #include "InputSender.h"
 #include "RemoteClient.h"
 
 InputSender::InputSender(RemoteClient* _client) : client(_client) {
 }
 
 bool InputSender::MouseMove(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_MOVE, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseLeftDown(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_LEFT_DOWN, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseLeftUp(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_LEFT_UP, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseRightDown(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_RIGHT_DOWN, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseRightUp(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_RIGHT_UP, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseMiddleDown(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_MIDDLE_DOWN, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseMiddleUp(int x, int y) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_MIDDLE_UP, x, y, 0);
     return true;
 }
 
 bool InputSender::MouseWheel(int x, int y, int delta) {
     if (!client) return false;
     client->QueueMouse(CMD_MOUSE_WHEEL, x, y, delta);
     return true;
 }
 
 bool InputSender::KeyDown(WORD virtualKey) {
     if (!client) return false;
     client->QueueKey(CMD_KEY_DOWN, virtualKey, (WORD)MapVirtualKey(virtualKey, 0), 0);
     return true;
 }
 
 bool InputSender::KeyUp(WORD virtualKey) {
     if (!client) return false;
     client->QueueKey(CMD_KEY_UP, virtualKey, (WORD)MapVirtualKey(virtualKey, 0), KEYEVENTF_KEYUP);
     return true;
 }
