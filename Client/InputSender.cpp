 #include "InputSender.h"
 #include "RemoteClient.h"
 
 InputSender::InputSender(RemoteClient* _client) : client(_client) {
 }
 
 bool InputSender::MouseMove(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_MOVE;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseLeftDown(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_LEFT_DOWN;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseLeftUp(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_LEFT_UP;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseRightDown(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_RIGHT_DOWN;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseRightUp(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_RIGHT_UP;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseMiddleDown(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_MIDDLE_DOWN;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseMiddleUp(int x, int y) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_MIDDLE_UP;
 
     DataMouse data;
     data.x = x;
     data.y = y;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::MouseWheel(int x, int y, int delta) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_MOUSE_WHEEL;
 
     DataMouse data;
     data.x = x;
     data.y = y;
     data.wheelDelta = delta;
 
     return client->SendCommand(cmd, &data, sizeof(DataMouse));
 }
 
 bool InputSender::KeyDown(WORD virtualKey) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_KEY_DOWN;
 
     DataKey data;
     data.virtualKey = virtualKey;
     data.scanCode = MapVirtualKey(virtualKey, 0);
     data.flags = 0;
 
     return client->SendCommand(cmd, &data, sizeof(DataKey));
 }
 
 bool InputSender::KeyUp(WORD virtualKey) {
     if (!client) return false;
 
     FrameCmd cmd;
     cmd.cmd = CMD_KEY_UP;
 
     DataKey data;
     data.virtualKey = virtualKey;
     data.scanCode = MapVirtualKey(virtualKey, 0);
     data.flags = KEYEVENTF_KEYUP;
 
     return client->SendCommand(cmd, &data, sizeof(DataKey));
 }
