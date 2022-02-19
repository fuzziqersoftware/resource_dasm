#include "TrapInfo.hh"

#include <map>
#include <unordered_map>
#include <vector>
#include <stdexcept>

using namespace std;



static const map<uint32_t, const char*> addr_to_global_name({
  {0x0000, "__m68k_reset_stack__"}, // stack ptr for reset vector
  {0x0004, "__m68k_vec_reset__"}, // reset vector
  {0x0008, "BusErrVct"}, // bus error vector
  {0x000C, "__m68k_vec_address_error__"}, // address error vector
  {0x0010, "__m68k_vec_illegal__"}, // illegal instruction vector
  {0x0014, "__m68k_vec_div_zero__"}, // divide by zero vector
  {0x0018, "__m68k_vec_chk__"}, // CHK instruction vector
  {0x001C, "__m68k_vec_trapv__"}, // TRAPV instruction vector
  {0x0020, "__m68k_vec_priv_violation__"}, // privilege violation vector
  {0x0024, "__m68k_vec_trace__"}, // trace interrupt vector
  {0x0028, "__m68k_vec_a_trap__"}, // line 1010 emulator vector
  {0x002C, "__m68k_vec_f_trap__"}, // line 1111 emulator vector
  {0x003C, "__m68k_vec_uninitialized__"}, // uninitialized interrupt vector
  {0x0060, "__m68k_vec_spurious__"}, // spurious interrupt vector
  {0x0064, "__m68k_vec_via__"}, // VIA interrupt vector
  {0x0068, "__m68k_vec_scc__"}, // SCC interrupt vector
  {0x006C, "__m68k_vec_via_scc__"}, // VIA+SCC vector (temporary)
  {0x0070, "__m68k_vec_switch__"}, // interrupt switch vector
  {0x0074, "__m68k_vec_switch_via__"}, // interrupt switch + VIA vector
  {0x0078, "__m68k_vec_switch_scc__"}, // interrupt switch + SCC vector
  {0x007C, "__m68k_vec_switch_via_scc__"}, // interrupt switch + VIA + SCC vector
  {0x0100, "MonkeyLives"}, // monkey alive if >= 0 (word)
  {0x0102, "ScrVRes"}, // screen vertical dots/inch (word)
  {0x0104, "ScrHRes"}, // screen horizontal dots/inch (word)
  {0x0106, "ScreenRow"}, // RowBytes of screen (word)
  {0x0108, "MemTop"}, // top of memory; on Mac XL, top of memory available to applications (ptr)
  {0x010C, "BufPtr"}, // address of end of jump table / top of application memory (ptr)
  {0x0110, "StkLowPt"}, // lowest stack as measured in VBL task (ptr)
  {0x0114, "HeapEnd"}, // end of application heap zone (ptr)
  {0x0118, "TheZone"}, // current heap zone (ptr)
  {0x011C, "UTableBase"}, // unit i/o table (ptr)
  {0x0120, "MacJump"},
  {0x0124, "DskRtnAdr"},
  {0x0128, "PollRtnAdr"},
  {0x012C, "DskVerify"}, // used by 3.5" disk driver for read/verify (byte)
  {0x012D, "LoadTrap"}, // trap before launch flag (byte)
  {0x012E, "MmInOK"}, // initial memory manager checks ok flag (byte)
  {0x012F, "CPUFlag"}, // 0x00 = 68000, 0x01 = 68010, 0x02 = 68020, ...? (byte)
  {0x0130, "ApplLimit"}, // application heap limit (ptr)
  {0x0134, "SonyVars"},
  {0x0138, "PWMValue"},
  {0x013A, "PollStack"},
  {0x013E, "PollProc"},
  {0x0142, "DskErr"}, // disk routine result code (word)
  {0x0144, "SysEvtMask"}, // system event mask (word)
  {0x0146, "SysEvtBuf"}, // system event queue element buffer (ptr)
  {0x014A, "EventQueue"}, // event queue header (10 bytes)
  {0x0154, "EvtBufCnt"}, // max number of events in SysEvtBuf minus 1 (word)
  {0x0156, "RndSeed"}, // random seed/number (long)
  {0x015A, "SysVersion"}, // version number of RAM-based system (word)
  {0x015C, "SEvtEnb"}, // enable SysEvent calls from GetNextEvent; 0 if SysEvent should return false (byte)
  {0x015D, "DSWndUpdate"}, // GetNextEvent not to paint behind DS AlertRect? (byte)
  {0x015E, "FontFlag"},
  {0x015F, "IntFlag"}, // reduce interrupt disable time when bit 7 == 0 (byte)
  {0x0160, "VBLQueue"}, // VBL (vertical retrace) queue header (10 bytes)
  {0x016A, "Ticks"}, // tick count; time since boot (long)
  {0x016E, "MBTicks"}, // tick count at last mouse button (long)
  {0x0172, "MBState"}, // current mouse button state (byte)
  {0x0173, "Tocks"},
  {0x0174, "KeyMap"}, // bitmap of the keyboard (2 longs)
  {0x017C, "KeypadMap"}, // bitmap of the numeric keypad (18 bits, stored as long)
  {0x0184, "KeyLast"}, // ASCII code of last keypress (word)
  {0x0186, "KeyTime"}, // tick count when KeyLast was written (word)
  {0x018A, "KeyRepTime"}, // tick count when key was last repeated (word)
  {0x018E, "KeyThresh"}, // threshold for key repeat (word)
  {0x0190, "KeyRepThresh"}, // key repeat speed (word)
  {0x0192, "Lvl1DT"}, // Level-1 secondary interrupt vector table (32 bytes)
  {0x01B2, "Lvl2DT"}, // Level-2 secondary interrupt vector table (32 bytes)
  {0x01D2, "UnitNtryCnt"}, // count of entries in unit table (word)
  {0x01D4, "VIA"}, // VIA base addr (ptr)
  {0x01D8, "SCCRd"}, // SCC read base addr (ptr)
  {0x01DC, "SCCWr"}, // SCC write base addr (ptr)
  {0x01E0, "IWM"}, // IWM base addr (ptr)
  {0x01E4, "GetParam/Scratch20"}, // system parameter scratch (20 bytes)
  {0x01F8, "SPValid/SysParam"}, // validation field (== 0xA7) (byte); start of low-memory copy of parameter memory (0x14 bytes)
  {0x01F9, "SPATalkA"}, // AppleTalk node number hint for port A (modem) (byte)
  {0x01FA, "SPATalkB"}, // AppleTalk node number hint for port B (printer) (byte)
  {0x01FB, "SPConfig"}, // config bits (4-7 port A, 0-3 port B)
  {0x01FC, "SPPortA"}, // SCC port A config (word)
  {0x01FE, "SPPortB"}, // SCC port B config (word)
  {0x0200, "SPAlarm"}, // alarm time (long)
  {0x0204, "SPFont"}, // default application font number minus 1 (word)
  {0x0206, "SPKbd"}, // keyboard repeat thresholds in 4/60ths (2x 4-bit)
  {0x0207, "SPPrint"}, // print stuff (byte)
  {0x0208, "SPVolCtl"}, // volume control (byte)
  {0x0209, "SPClikCaret"}, // double-click/caret time in 4/60ths (2x 4-bit)
  {0x020A, "SPMisc1"}, // miscellaneous (byte)
  {0x020B, "SPMisc2/PCDeskPat"}, // top bit is PCDeskPat; mouse scaling, sys startup disk, menu blink flags (byte)
  {0x020C, "Time"}, // clock time; extrapolated (long)
  {0x0210, "BootDrive"}, // drive number of boot drive (word)
  {0x0212, "JShell"},
  {0x0214, "SFSaveDisk"}, // negative of volume reference number used by Standard File (word)
  {0x0216, "KbdVars/HiKeyLast"}, // keybaord manager variables (4 bytes)
  {0x0218, "KbdLast"},
  {0x021A, "JKybdTask"}, // keyboard VBL task hook (ptr)
  {0x021E, "KbdType"}, // keyboard model number (byte)
  {0x021F, "AlarmState"}, // bit 7 = parity, bit 6 = beeped, bit 0 = enable (byte)
  {0x0220, "MemErr"}, // last memory manager error (word)
  {0x0222, "JFigTrkSpd"},
  {0x0226, "JDiskPrime"},
  {0x022A, "JRdAddr"},
  {0x022E, "JRdData"},
  {0x0232, "JWrData"},
  {0x0236, "JSeek"},
  {0x023A, "JSetupPoll"},
  {0x023E, "JRecal"},
  {0x0242, "JControl"},
  {0x0246, "JWakeUp"},
  {0x024A, "JReSeek"},
  {0x024E, "JMakeSpdTbl"},
  {0x0252, "JAdrDisk"},
  {0x0256, "JSetSpeed"},
  {0x025A, "NiblTbl"},
  {0x025E, "FlEvtMask"},
  {0x0260, "SdVolume"}, // global volume control; low 3 bits only (byte)
  {0x0261, "SdEnable/Finder"},
  {0x0262, "SoundPtr/SoundVars"}, // ptr to 4-tone record 4VE / sound definition table (ptr)
  {0x0266, "SoundBase"}, // ptr to free-form synth buffer / sound bitmap (ptr)
  {0x026A, "SoundVBL"}, // vertical retrace control element (16 bytes)
  {0x027A, "SoundDCE"}, // sound driver DCE (ptr)
  {0x027E, "SoundActive"}, // sound is active flag (byte)
  {0x027F, "SoundLevel"}, // amplitude in 740-byte buffer (byte)
  {0x0280, "CurPitch"}, // value of count in square-wave synth buffer (word)
  {0x0282, "Switcher"},
  {0x0286, "SwitcherTPtr"}, // switcher's switch table
  {0x028A, "RSDHndl"},
  {0x028E, "ROM85"}, // high bit is 0 for rom 0x75 [sic] and later (word)
  {0x0290, "PortAUse"}, // bit 7: 1 = port A not in use, 0 = in use
  {0x0291, "PortBUse"}, // bit 7: 1 = port B not in use, 0 = in use
  {0x0292, "ScreenVars"},
  {0x029A, "JGNEFilter"}, // GetNextEvent filter procedure (ptr)
  {0x029E, "Key1Trans"}, // keyboard translator procedure (ptr)
  {0x02A2, "Key2Trans"}, // numeric keypad translator procedure (ptr)
  {0x02A6, "SysZone"}, // system heap zone (ptr)
  {0x02AA, "ApplZone"}, // application heap zone (ptr)
  {0x02AE, "ROMBase"}, // ROM base addr (ptr)
  {0x02B2, "RAMBase"}, // trap dispatch table's base address for routines in RAM (ptr)
  {0x02B6, "ExpandMem"}, // ptr to expanded memory block
  {0x02BA, "DSAlertTab"}, // system error alerts table (ptr)
  {0x02BE, "ExtStsDT"}, // external/status interrupt vector table (16 bytes)
  {0x02CE, "SCCASts"},
  {0x02CF, "SCCBSts"},
  {0x02D0, "SerialVars"}, // asynchronous driver variables (16 bytes)
  {0x02D8, "ABusVars"}, // ptr to AppleTalk local vars
  {0x02DC, "ABusDCE"}, // ptr to AppleTalk DCE
  {0x02E0, "FinderName"}, // likely: name of finder application (char[0x10]; p-string)
  {0x02F0, "DoubleTime"}, // double-click ticks (long)
  {0x02F4, "CaretTime"}, // caret blink ticks (long)
  {0x02F8, "ScrDmpEnb"}, // screen dump enabled flag (bool as byte)
  {0x02F9, "ScrDmpType"}, // 0xFF = dump screen, 0xFE = dump front window (byte)
  {0x02FA, "TagData"}, // sector tag info for disk drivers (14 bytes)
  {0x02FC, "BufTgFNum"}, // file tags buffer: file number (long)
  {0x0300, "BufTgFFlg"}, // file tags buffer: flags (word)
  {0x0302, "BufTgFBkNum"}, // file tags buffer: logical block number (word)
  {0x0304, "BufTgDate"}, // file tags buffer: timestamp (long)
  {0x0308, "DrvQHdr"}, // queue header of drives in system (10 bytes)
  {0x0312, "PWMBuf2"}, // PWM buffer 1 (or 2 if sound) (ptr)
  {0x0316, "HpChk/MacPgm"}, // heap check RAM code (ptr)
  // this looks like a relic from 24-bit addressing days; it's the memory
  // manager byte count / handle / ptr mask, defined as 0x00FFFFFF in some docs
  {0x031A, "MaskBC/MaskHandle/MaskPtr/Lo3Bytes"}, // (long)
  {0x031E, "MinStack"}, // minimum stack size used in InitApplZone (long)
  {0x0322, "DefltStack"}, // default stack size (long)
  {0x0326, "MMDefFlags"}, // default zone flags (word)
  {0x0328, "GZRootHnd"}, // root handle for grow zone (handle)
  {0x032C, "GZRootPtr"}, // root ptr for grow zone (ptr)
  {0x0330, "GZMoveHnd"}, // moving handle for grow zone (handle)
  {0x0334, "DSDrawProc"}, // alternate SysError draw procedure (ptr)
  {0x0338, "EjectNotify"}, // eject notify procedure (ptr)
  {0x033C, "IAZNotify"}, // world swaps notify procedure (ptr)
  {0x0340, "CurDB"},
  {0x0342, "NxtDB"},
  {0x0344, "MaxDB"},
  {0x0346, "FlushOnly"},
  {0x0347, "RegRsrc"},
  {0x0348, "FLckUnlck"},
  {0x0349, "FrcSync"},
  {0x034A, "NewMount"},
  {0x034B, "NoEject"},
  {0x034C, "DrMstrBlk"},
  {0x034E, "FCBSPtr"},
  {0x0352, "DefVCBPtr"},
  {0x0356, "VCBQHdr"},
  {0x0360, "FSQHdr"},
  {0x0362, "FSQHead"},
  {0x0366, "FSQTail"},
  {0x036A, "HFSStkTop"},
  {0x036E, "HFSStkPtr"},
  {0x0372, "WDCBsPtr"},
  {0x0376, "HFSFlags"},
  {0x0377, "CacheFlag"},
  {0x0378, "SysBMCPtr"},
  {0x037C, "SysVolCPtr"},
  {0x0380, "SysCtlCPtr"},
  {0x0384, "DefVRefNum"},
  {0x0386, "PMSPPtr"},
  {0x038A, "HFSTagData"},
  {0x0392, "HFSDSErr"},
  {0x0394, "CacheVars"},
  {0x0398, "CurDirStore"}, // save directory across calls to Standard File (long)
  {0x039C, "CacheCom"},
  {0x039E, "FmtDefaults"},
  {0x03A2, "ErCode"},
  {0x03A4, "Params"},
  {0x03D6, "FSTemp8"},
  {0x03DE, "FSIOErr"},
  {0x03E2, "FSQueueHook"},
  {0x03E6, "ExtFSHook"},
  {0x03EA, "DskSwtchHook"},
  {0x03EE, "ReqstVol"},
  {0x03F2, "ToExtFS"},
  {0x03F6, "FSFCBLen"},
  {0x03F8, "DSAlertRect"}, // rect for disk-switch or system-error alert (8 bytes)
  {0x0800, "JHideCrsr"},
  {0x0804, "JShowCrsr"},
  {0x0808, "JShieldCrsr"},
  {0x080C, "JScrnAddr"},
  {0x0810, "JScrnSize"},
  {0x0814, "JInitCrsr"},
  {0x0818, "JSetCrsr"},
  {0x081C, "JCrsrObscure"},
  {0x0820, "JUpdateProc"},
  {0x0824, "ScrnBase"}, // main screen buffer (ptr)
  {0x0828, "MTemp"}, // low-level interrupt mouse location (long)
  {0x082C, "RawMouse"}, // un-jerked mouse coordinates (long)
  {0x0830, "Mouse"}, // processed mouse coordinates (long)
  {0x0834, "CrsrPin"}, // cursor pinning rect (4x word)
  {0x083C, "CrsrRect"}, // cursor hit rect (4x word)
  {0x0844, "TheCrsr"}, // cursor data; mask & hotspot (0x44 bytes)
  {0x0888, "CrsrAddr"}, // address of data under cursor (long)
  {0x088C, "CrsrSave/JAllocCrsr/NewCrsrJTbl"}, // data under the cursor (64 bytes) / vector to routine that allocates cursor (long) / location of new cursor jump vectors
  {0x0890, "JSetCCrsr"}, // vector to routine that sets color cursor (long)
  {0x0894, "JOpcodeProc"}, // vector to process new picture opcodes (long)
  {0x0898, "CrsrBase"}, // scrnBase for cursor (long)
  {0x089C, "CrsrDevice"}, // current cursor device (long)
  {0x08A0, "SrcDevice"}, // source device for stretchBits (long)
  {0x08A4, "MainDevice"}, // the main screen device (long)
  {0x08A8, "DeviceList"}, // list of display devices (long)
  {0x08AC, "CrsrRow"}, // rowBytes for current cursor screen (word)
  {0x08B0, "QDColors"}, // handle to default colors (long)
  {0x08CC, "CrsrVis"}, // cursor visible flag (byte)
  {0x08CD, "CrsrBusy"}, // cursor locked out flag (byte)
  {0x08CE, "CrsrNew"}, // cursor changed flag (byte)
  {0x08CF, "CrsrCouple"}, // cursor coupled to mouse flag (byte)
  {0x08D0, "CrsrState"}, // cursor nesting level (word)
  {0x08D2, "CrsrObscure"}, // cursor obscure semaphore (byte)
  {0x08D3, "CrsrScale"}, // cursor scaled flag (byte)
  {0x08D6, "MouseMask"}, // V-H mask for ANDing with mouse (long)
  {0x08DA, "MouseOffset"}, // V-H offset for adding after ANDing (long)
  {0x08DE, "JournalFlag"}, // journaling mode/state (word)
  {0x08E0, "JSwapFont"},
  {0x08E4, "JFontInfo"},
  {0x08E8, "JournalRef"}, // reference number of journaling device driver (word)
  {0x08EC, "CrsrThresh"}, // mouse-scaling delta threshold (word)
  {0x08EE, "JCrsrTask"}, // address of CrsrVBLTask (long)
  {0x08F2, "WWExist"}, // window manager initialized flag (byte)
  {0x08F3, "QDExist"}, // QuickDraw is initialized flag (byte)
  {0x08F4, "JFetch"}, // fetch-a-byte routine for drivers (ptr)
  {0x08F8, "JStash"}, // stash-a-byte routine for drivers (ptr)
  {0x08FC, "JIODone"}, // IODone entry location (ptr)
  {0x0900, "CurApRefNum"}, // reference number of application's resource file (word)
  {0x0902, "LaunchFlag"}, // rom launch or chain flag (byte)
  {0x0903, "FondState"},
  {0x0904, "CurrentA5"}, // current value of A5: addr of boundary between application globals and application parameters (ptr)
  {0x0908, "CurStackBase"}, // current stack base; start of application globals (ptr)
  {0x090C, "LoadFiller"},
  {0x0910, "CurApName"}, // name of application (char[0x20]; p-string)
  {0x0930, "SaveSegHandle"}, // segment 0 handle
  {0x0934, "CurJTOffset"}, // current jump table offset from A5 (word)
  {0x0936, "CurPageOption"}, // current page 2 configuration / sound/screen buffer configuration passed to Chain or Launch (word)
  {0x0938, "HiliteMode"}, // used for color highlighting (word)
  {0x093A, "LoaderPBlock"}, // parameter block for ExitToShell (10 bytes)
  {0x0944, "PrintErr"},
  {0x0946, "ChooserBits"},
  {0x0946, "PrFlags"},
  {0x0947, "PrType"},
  {0x0952, "PrRefNum"},
  {0x0954, "LastPGlobal"},
  {0x0960, "ScrapSize/ScrapInfo/ScrapVars"}, // scrap length (long), also start of scrap variables
  {0x0964, "ScrapHandle"}, // memory scrap (handle)
  {0x0968, "ScrapCount"}, // validation byte (word)
  {0x096A, "ScrapState"}, // scrap state (word)
  {0x096C, "ScrapName"}, // ptr to scrap name
  {0x0970, "ScrapTag"}, // scrap filename (char[16])
  {0x0980, "RomFont0/ScrapEnd"}, // ???, also end of scrap variables
  {0x0984, "AppFontID"},
  {0x0986, "SaveFondFlags"},
  {0x0987, "FMDefaultSize"},
  {0x0988, "CurFMFamily"},
  {0x098A, "CurFMSize"},
  {0x098C, "CurFMFace"},
  {0x098D, "CurFMNeedBits"},
  {0x098E, "CurFMDevice"},
  {0x0990, "CurFMNumer"},
  {0x0994, "CurFMDenom"},
  {0x0998, "FOutError"},
  {0x099A, "FOutFontHandle"},
  {0x099E, "FOutBold"},
  {0x099F, "FOutItalic"},
  {0x09A0, "FOutULOffset"},
  {0x09A1, "FOutULShadow"},
  {0x09A2, "FOutULThick"},
  {0x09A3, "FOutShadow"},
  {0x09A4, "FOutExtra"},
  {0x09A5, "FOutAscent"},
  {0x09A6, "FOutDescent"},
  {0x09A7, "FOutWidMax"},
  {0x09A8, "FOutLeading"},
  {0x09A9, "FOutUnused"},
  {0x09AA, "FOutNumer"},
  {0x09AE, "FOutDenom"},
  {0x09B2, "FMDotsPerInch"},
  {0x09B6, "FMStyleTab"},
  {0x09CE, "ToolScratch"},
  {0x09D6, "WindowList"}, // z-ordered linked list of windows; null if using events but not windows (ptr)
  {0x09DA, "SaveUpdate"},
  {0x09DC, "PaintWhite"}, // erase window with white before update event flag (word)
  {0x09DE, "WMgrPort"}, // window manager's grafPort (ptr)
  {0x09E2, "DeskPort"},
  {0x09E6, "OldStructure"},
  {0x09EA, "OldContent"},
  {0x09EE, "GrayRgn"}, // rounded gray desk region (handle)
  {0x09F2, "SaveVisRgn"},
  {0x09F6, "DragHook"}, // user hook during dragging (procedure to execute during TrackGoAway, DragWindow, GrowWindow, DragGrayRgn, TrackControl, and DragControl) (ptr)
  {0x09FA, "TempRect/Scratch8"}, // 8-byte scratch area
  {0x0A02, "OneOne"}, // 0x00010001 (long)
  {0x0A06, "MinusOne"}, // 0xFFFFFFFF (long)
  {0x0A0A, "TopMenuItem"},
  {0x0A0C, "AtMenuBottom"},
  {0x0A0E, "IconBitmap"},
  {0x0A1C, "MenuList"},
  {0x0A20, "MBarEnable"},
  {0x0A22, "CurDeKind"},
  {0x0A24, "MenuFlash"},
  {0x0A26, "TheMenu"},
  {0x0A28, "SavedHandle"},
  {0x0A2C, "MBarHook"},
  {0x0A30, "MenuHook"},
  {0x0A34, "DragPattern"},
  {0x0A3C, "DeskPattern"}, // desk pattern (8 bytes)
  {0x0A44, "DragFlag"},
  {0x0A46, "CurDragAction"},
  {0x0A4A, "FPState"},
  {0x0A50, "TopMapHndl"}, // topmost (most recently opened) resource map in list (handle)
  {0x0A54, "SysMapHndl"}, // system resource map handle
  {0x0A58, "SysMap"}, // reference number of system resource map (word)
  {0x0A5A, "CurMap"}, // reference number of current resource map (word)
  {0x0A5C, "ResReadOnly"}, // resource read-only flag (word)
  {0x0A5E, "ResLoad"}, // current SetResLoad state (word)
  {0x0A60, "ResErr"}, // resource error code (word)
  {0x0A62, "TaskLock"},
  {0x0A63, "FScaleDisable"},
  {0x0A64, "CurActivate"}, // window slated for activate event (ptr)
  {0x0A68, "CurDeactive"}, // window slated for deactivate event (ptr)
  {0x0A6C, "DeskHook"}, // hook for painting desktop or responding to clicks on desktop (ptr)
  {0x0A70, "TEDoText"}, // TextEdit multi-purpose routine (ptr)
  {0x0A74, "TERecal"}, // TextEdit recalculate line starts routine (ptr)
  {0x0A78, "ApplScratch"},
  {0x0A84, "GhostWindow"}, // window hidden from FrontWindow; never to be considered frontmost (ptr)
  {0x0A88, "CloseOrnHook"},
  {0x0A8C, "RestProc/ResumeProc"}, // resume procedure from InitDialogs (ptr)
  {0x0A90, "SaveProc"},
  {0x0A94, "SaveSP"},
  {0x0A98, "ANumber"},
  {0x0A9A, "ACount"},
  {0x0A9C, "DABeeper"},
  {0x0AA0, "DAStrings"},
  {0x0AB0, "TEScrpLength"}, // TextEdit scrap length (word)
  {0x0AB4, "TEScrpHandle"}, // TextEdit scrap (handle)
  {0x0AB8, "AppPacks"},
  {0x0AD8, "SysResName"}, // name of system resource file (char[0x10]; p-string)
  {0x0AE8, "SoundGlue"},
  {0x0AEC, "AppParmHandle"},
  {0x0AF0, "DSErrCode"}, // last system error alert id (word)
  {0x0AF2, "ResErrProc"}, // resource error procedure (ptr)
  {0x0AF6, "TEWdBreak"}, // default word break routine (ptr)
  {0x0AFA, "DlgFont"},
  {0x0AFC, "LastTGlobal"},
  {0x0B00, "TrapAgain"},
  {0x0B04, "KeyMVars"}, // for ROM KEYM procedure state (word)
  {0x0B06, "ROMMapHndl"}, // handle of ROM resource map (long)
  {0x0B0A, "PWMBuf1"},
  {0x0B0E, "BootMask"},
  {0x0B10, "WidthPtr"},
  {0x0B14, "ATalkHk1"},
  {0x0B18, "LAPMgrPtr"},
  {0x0B1C, "FourDHack"},
  {0x0B20, "UnSwitchedFlags"},
  {0x0B21, "SwitchedFlags"},
  {0x0B22, "HWCfgFlags"},
  {0x0B24, "TimeSCSIDB"},
  {0x0B26, "Top2MenuItem"},
  {0x0B28, "At2MenuBottom"},
  {0x0B2A, "WidthTabHandle"},
  {0x0B2E, "SCSIDrvrs"},
  {0x0B30, "TimeVars"},
  {0x0B34, "BtDskRfn"},
  {0x0B36, "BootTmp8"},
  {0x0B3E, "NTSC"},
  {0x0B3F, "T1Arbitrate"},
  {0x0B40, "JDiskSel"},
  {0x0B44, "JSendCmd"},
  {0x0B48, "JDCDReset"},
  {0x0B4C, "LastSPExtra"},
  {0x0B50, "FileShareVars"},
  {0x0B54, "MenuDisable"},
  {0x0B58, "MBDFHndl"},
  {0x0B5C, "MBSaveLoc"},
  {0x0B60, "BNMQHdr"},
  {0x0B64, "BackgrounderVars"},
  {0x0B68, "MenuLayer"},
  {0x0B6C, "OmegaSANE"},
  {0x0B72, "CarlByte"},
  {0x0B73, "SystemInfo"},
  {0x0B78, "VMGlobals"},
  {0x0B7C, "Twitcher2"},
  {0x0B80, "RMgrHiVars"},
  {0x0B84, "HSCHndl"},
  {0x0B88, "PadRsrc"},
  {0x0B9A, "ResOneDeep"},
  {0x0B9C, "PadRsrc2"},
  {0x0B9E, "RomMapInsert"}, // necessary to link resource map to ROM resource map flag (byte)
  {0x0B9F, "TmpResLoad"}, // temporary ResLoad value? (byte)
  {0x0BA0, "IntlSpec"}, // ptr to extra international data
  {0x0BA4, "RMgrPerm"},
  {0x0BA5, "WordRedraw"}, // used by TextEdit RecalDraw (byte)
  {0x0BA6, "SysFontFam"},
  {0x0BA8, "DefFontSize"},
  {0x0BAA, "MBarHeight"}, // menu bar height
  {0x0BAC, "TESysJust"}, // system text justification for International TextEdit (word)
  {0x0BAE, "HiHeapMark"}, // highest address used by a zone below the stack ptr (long)
  {0x0BB2, "SegHiEnable"}, // 0 = disable MoveHHi in LoadSeg (byte)
  {0x0BB3, "FDevDisable"},
  {0x0BB4, "CommToolboxGlob"}, // ptr to CommToolbox globals
  {0x0BB4, "CMVector"},
  {0x0BBC, "ShutDwnQHdr"},
  {0x0BC0, "NewUnused"},
  {0x0BC2, "LastFOND"},
  {0x0BC6, "FONDID"},
  {0x0BC8, "App2Packs"},
  {0x0BE8, "MAErrProc"},
  {0x0BEC, "MASuperTab"},
  {0x0BF0, "MimeGlobs"},
  {0x0BF4, "FractEnable"},
  {0x0BF5, "UsedFWidth"},
  {0x0BF6, "FScaleHFact"},
  {0x0BFA, "FScaleVFact"},
  {0x0BFE, "SCCIOPFlag"},
  {0x0BFF, "MacJmpFlag"},
  {0x0C00, "SCSIBase"}, // base address for SCSI chip read (long)
  {0x0C04, "SCSIDMA"}, // base address for SCSI DMA (long)
  {0x0C08, "SCSIHsk"}, // base address for SCSI handshake (long)
  {0x0C0C, "SCSIGlobals"}, // ptr to SCSI manager globals
  {0x0C10, "RGBBlack"}, // the black field for color (3x word)
  {0x0C16, "RGBWhite"}, // the white field for color (3x word)
  {0x0C1C, "FMSynth"},
  {0x0C20, "RowBits"}, // screen horizontal poxels (word)
  {0x0C22, "ColLines"}, // screen vertical pixels (word)
  {0x0C24, "ScreenBytes"}, // total screen bytes (long)
  {0x0C28, "IOPMgrVars"},
  {0x0C2C, "NMIFlag"}, // flag for NMI debounce (byte)
  {0x0C2D, "VidType"}, // video board type id (byte)
  {0x0C2E, "VidMode"}, // video mode (4 = 4-bit color) (byte)
  {0x0C2F, "SCSIPoll"}, // poll for device zero only once flag (byte)
  {0x0C30, "SEVarBase"},
  {0x0C6C, "MacsBugSP"},
  {0x0C70, "MacsBugPC"},
  {0x0C74, "MacsBugSR"},
  {0x0CB0, "MMUFlags"}, // reserved; zero (byte)
  {0x0CB1, "MMUType"}, // type of MMU (byte)
  {0x0CB2, "MMU32bit"}, // current machine MMU mode (bool as byte)
  {0x0CB3, "MMUFluff/MachineType"}, // no longer used (byte)
  {0x0CB4, "MMUTbl24/MMUTbl"}, // ptr to MMU mapping table
  {0x0CB8, "MMUTbl32/MMUTblSize"}, // size of MMU mapping table (long)
  {0x0CBC, "SInfoPtr"}, // ptr to Slot Manager info
  {0x0CC0, "ASCBase"}, // ptr to sound chip
  {0x0CC4, "SMGlobals"}, // ptr to Sound Manager globals
  {0x0CC8, "TheGDevice"}, // the current graphics device (long)
  {0x0CCC, "CQDGlobals"}, // QuickDraw global extensions (long)
  {0x0CD0, "AuxWinHead"},
  {0x0CD4, "AuxCtlHead"},
  {0x0CD8, "DeskCPat"},
  {0x0CDC, "SetOSDefKey"},
  {0x0CE0, "LastBinPat"},
  {0x0CE8, "DeskPatEnable"},
  {0x0CEA, "TimeVIADB"},
  {0x0CEC, "VIA2Base"},
  {0x0CF0, "VMVectors"},
  {0x0CF8, "ADBBase"}, // ptr to ADB globals
  {0x0CFC, "WarmStart"}, // warm start flag (long)
  {0x0D00, "TimeDBRA"}, // CPU speed: number of iterations of DBRA per millisecond (word)
  {0x0D02, "TimeSCCDB"}, // CPU speed: number of iterations of SCC access and DBRA (word)
  {0x0D04, "SlotQDT"}, // ptr to slot queue table
  {0x0D08, "SlotPrTbl"}, // ptr to slot priority table
  {0x0D0C, "SlotVBLQ"}, // ptr to slot VBL queue table
  {0x0D10, "ScrnVBLPtr"}, // save for ptr to main screen VBL queue
  {0x0D14, "SlotTICKS"}, // ptr to slot tick count table
  {0x0D18, "PowerMgrVars"},
  {0x0D1C, "AGBHandle"},
  {0x0D20, "TableSeed"}, // seed value for color table IDs (long)
  {0x0D24, "SRsrcTblPtr"}, // ptr to slot resource table
  {0x0D28, "JVBLTask"}, // vector to slot VBL task interrupt handler
  {0x0D2C, "WMgrCPort"}, // window manager color port (long)
  {0x0D30, "VertRRate"}, // vertical refresh rate for start manager (word)
  {0x0D32, "SynListHandle"},
  {0x0D36, "LastFore"},
  {0x0D3A, "LastBG"},
  {0x0D3E, "LastMode"},
  {0x0D40, "LastDepth"},
  {0x0D42, "FMExist"},
  {0x0D43, "SavedHilite"},
  {0x0D4C, "ShieldDepth"},
  {0x0D50, "MenuCInfo"},
  {0x0D54, "MBProcHndl"},
  {0x0D5C, "MBFlash"},
  {0x0D60, "ChunkyDepth"}, // pixel depth (word)
  {0x0D62, "CrsrPtr"}, // ptr to cursor save area
  {0x0D66, "PortList"}, // list of grafPorts (long)
  {0x0D6A, "MickeyBytes"}, // ptr to more cursor vars
  {0x0D6E, "QDErr"}, // QuickDraw error code (word)
  {0x0D70, "VIA2DT"}, // VIA2 dispatch table for NuMac (32 bytes)
  {0x0D90, "SInitFlags"}, // StartInit.a flags (word)
  {0x0D92, "DTQFlags/DTQueue"}, // deferred task queue header; task queue flags (word)
  {0x0D94, "DTskQHdr"}, // queue head ptr
  {0x0D98, "DTskQTail"}, // queue tail ptr
  {0x0D9C, "JDTInstall"}, // ptr to install deferred task routine
  {0x0DA0, "HiliteRGB"}, // highlight color (3x word)
  {0x0DA6, "OldTimeSCSIDB"}, // number of iterations of SCSI access & DBRA (word)
  {0x0DA8, "DSCtrAdj"}, // center adjust for DS rect (long)
  {0x0DAC, "IconTLAddr"}, // ptr to where start icons go
  {0x0DB0, "VideoInfoOK"}, // signals to CritErr that video is ok (long)
  {0x0DB4, "EndSRTPtr"}, // ptr to end of slot reosurce table (not SRT buffer)
  {0x0DB8, "SDMJmpTblPtr"}, // ptr to SDM jump table
  {0x0DBC, "JSwapMMU"}, // vector to SwapMMU routine
  {0x0DC0, "SdmBusErr"}, // ptr to SDM bus error handler
  {0x0DC4, "LastTxGDevice"}, // copy of TheGDevice set up for fast text measuring (long)
  {0x0DC8, "PMgrHandle"},
  {0x0DCC, "LayerPalette"},
  {0x0DD0, "AddrMapFlags"},
  {0x0DD4, "UnivROMFlags"},
  {0x0DD8, "UniversalInfoPtr"},
  {0x0DDC, "BootGlobPtr"},
  {0x0DE0, "EgretGlobals"},
  {0x0DE4, "SaneTrapAddr"},
  {0x0DE8, "Warhol"},
  {0x1E00, "MemVectors24"},
  {0x1EE0, "Mem2Vectors24"},
  {0x1EF0, "Phys2Log"},
  {0x1EF4, "RealMemTop"},
  {0x1EF8, "PhysMemTop"},
  {0x1EFC, "MMFlags"},
  {0x1F00, "MemVectors32"},
  {0x1FB8, "DrawCrsrVector"},
  {0x1FBC, "EraseCrsrVector"},
  {0x1FC0, "PSCIntTbl"},
  {0x1FC4, "DSPGlobals"},
  {0x1FC8, "FP040Vects"},
  {0x1FCC, "FPBSUNVec"},
  {0x1FD0, "FPUNFLVec"},
  {0x1FD4, "FPOPERRVec"},
  {0x1FD8, "FPOVFLVec"},
  {0x1FDC, "FPSNANVec"},
  {0x1FE0, "Mem2Vectors32"},
  {0x1FF0, "SCSI2Base"},
  {0x1FF4, "LockMemCt"},
  {0x1FF8, "DockingGlobals"},
  {0x2000, "VectorPtr"},
  {0x2400, "BasesValid1"},
  {0x2404, "BasesValid2"},
  {0x2408, "ExtValid1"},
  {0x240C, "ExtValid2"},
});

const char* name_for_lowmem_global(uint32_t addr) {
  try {
    return addr_to_global_name.at(addr);
  } catch (const out_of_range&) {
    return nullptr;
  }
}
