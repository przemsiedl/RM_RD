 #ifndef InputExecutorH
 #define InputExecutorH
 
 #include <windows.h>
 #include "../Shared/Frame.h"
 
 class InputExecutor {
 public:
     static bool ExecuteMouseCommand(const FrameCmd& cmd, const DataMouse& data);
     static bool ExecuteKeyCommand(const FrameCmd& cmd, const DataKey& data);
 };
 
 #endif
