 #include "InputExecutor.h"
 
 bool InputExecutor::ExecuteMouseCommand(const FrameCmd& cmd, const DataMouse& data) {
     int screenWidth = GetSystemMetrics(SM_CXSCREEN);
     int screenHeight = GetSystemMetrics(SM_CYSCREEN);
 
     int absX = (data.x * 65535) / screenWidth;
     int absY = (data.y * 65535) / screenHeight;
 
     DWORD flags = MOUSEEVENTF_ABSOLUTE;
 
     switch (cmd.cmd) {
         case CMD_MOUSE_MOVE:
             flags |= MOUSEEVENTF_MOVE;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_LEFT_DOWN:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_LEFT_UP:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_RIGHT_DOWN:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTDOWN;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_RIGHT_UP:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTUP;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_MIDDLE_DOWN:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEDOWN;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_MIDDLE_UP:
             flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEUP;
             mouse_event(flags, absX, absY, 0, 0);
             break;
 
         case CMD_MOUSE_WHEEL:
             flags |= MOUSEEVENTF_WHEEL;
             mouse_event(flags, absX, absY, data.wheelDelta, 0);
             break;
 
         default:
             return false;
     }
 
     return true;
 }
 
 bool InputExecutor::ExecuteKeyCommand(const FrameCmd& cmd, const DataKey& data) {
     switch (cmd.cmd) {
         case CMD_KEY_DOWN:
             keybd_event(data.virtualKey, data.scanCode, 0, 0);
             break;
 
         case CMD_KEY_UP:
             keybd_event(data.virtualKey, data.scanCode, KEYEVENTF_KEYUP, 0);
             break;
 
         default:
             return false;
     }
 
     return true;
 }
