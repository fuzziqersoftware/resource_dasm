#include "TrapInfo.hh"

#include <map>
#include <unordered_map>
#include <vector>
#include <stdexcept>

using namespace std;



TrapInfo::TrapInfo(const char* name) : name(name) { }
TrapInfo::TrapInfo(const char* name,
    unordered_map<uint8_t, TrapInfo>&& flag_overrides,
    unordered_map<uint32_t, TrapInfo>&& subtrap_info,
    uint32_t proc_selector_mask)
  : name(name),
    proc_selector_mask(proc_selector_mask) {
  for (const auto& it : flag_overrides) {
    this->flag_overrides.emplace(it.first,
        shared_ptr<TrapInfo>(new TrapInfo(move(it.second))));
  }
  for (const auto& it : subtrap_info) {
    this->subtrap_info.emplace(it.first,
        shared_ptr<TrapInfo>(new TrapInfo(move(it.second))));
  }
}

const vector<TrapInfo> os_trap_info({
  // Seems that the H variants of these functions are used when flags=2. Is this
  // relevant? (Is the behavior different in that case?)
  {"Open/PBHOpen/HOpen", {{2, "OpenSlot"}}, {}}, // 0x00
  "Close", // 0x01
  "Read", // 0x02
  "Write", // 0x03
  "Control", // 0x04
  "Status", // 0x05
  "KillIO", // 0x06
  "GetVolInfo/PBHGetVInfo/HGetVInfo", // 0x07
  "Create/PBHCreate/HCreate", // 0x08
  "Delete/PBHDelete/HDelete", // 0x09
  "OpenRF/PBHOpenRF/HOpenRF", // 0x0A
  "Rename/PBHRename/HRename", // 0x0B
  "GetFileInfo/PBHGetFInfo/HGetFileInfo", // 0x0C
  "SetFileInfo/PBHSetFInfo/HSetFileInfo", // 0x0D
  "UnmountVol/HUnmountVol", // 0x0E
  "MountVol", // 0x0F
  "Allocate/PBAllocContig/AllocContig", // 0x10
  "GetEOF", // 0x11
  "SetEOF", // 0x12
  "FlushVol", // 0x13
  "GetVol/PBHGetVol/HGetVol", // 0x14
  "SetVol/PBHSetVol/HSetVol", // 0x15
  "InitQueue/FInitQueue", // 0x16
  "Eject", // 0x17
  "GetFPos", // 0x18
  "InitZone", // 0x19
  "GetZone", // 0x1A (called with flags as 0x11A)
  "SetZone", // 0x1B
  {"FreeMem", {{4, "FreeMemSys"}}, {}}, // 0x1C
  "MaxMem", // 0x1D (called with flags as 0x11D)
  {"NewPtr", { // 0x1E (called with flags as 0x11E)
    {3, "NewPtrClear"},
    {5, "NewPtrSys"},
    {7, "NewPtrSysClear"},
  }, {}},
  "DisposPtr/DisposePtr", // 0x1F
  "SetPtrSize", // 0x20
  "GetPtrSize", // 0x21
  {"NewHandle", { // 0x22 (called with flags as 0x122)
    {3, "NewHandleClear"},
    {5, "NewHandleSys"},
    {7, "NewHandleSysClear"},
  }, {}},
  "DisposHandle/DisposeHandle", // 0x23
  "SetHandleSize", // 0x24
  "GetHandleSize", // 0x25
  "HandleZone", // 0x26 (called with flags as 0x126)
  "ReallocHandle", // 0x27
  "RecoverHandle", // 0x28 (called with flags as 0x128)
  "HLock", // 0x29
  "HUnlock", // 0x2A
  "EmptyHandle", // 0x2B
  "InitApplZone", // 0x2C
  "SetApplLimit", // 0x2D
  "BlockMove/BlockMoveData", // 0x2E
  "PostEvent/PPostEvent", // 0x2F (called with flags as 0x12F)
  "OSEventAvail", // 0x30
  "GetOSEvent", // 0x31
  "FlushEvents", // 0x32
  "VInstall", // 0x33
  "VRemove", // 0x34
  "OffLine/Offline", // 0x35
  "MoreMasters", // 0x36
  "ReadParam", // 0x37
  "WriteParam", // 0x38
  "ReadDateTime", // 0x39
  "SetDateTime", // 0x3A
  "Delay", // 0x3B
  "CmpString", // 0x3C
  "DrvrInstall", // 0x3D
  "DrvrRemove", // 0x3E
  "InitUtil", // 0x3F
  {"ResrvMem/ReserveMem", {{4, "ReserveMemSys"}}, {}}, // 0x40
  "SetFilLock/PBHSetFLock/HSetFLock", // 0x41
  "RstFilLock/PBHRstFLock/HRstFLock", // 0x42
  "SetFilType", // 0x43
  "SetFPos", // 0x44
  "FlushFile", // 0x45
  {"GetTrapAddress", { // 0x46 (called with flags as 0x146)
    {3, "GetOSTrapAddress"},
    {7, "GetToolBoxTrapAddress/GetToolTrapAddress"},
  }, {}},
  {"SetTrapAddress", { // 0x47
    {2, "SetOSTrapAddress"},
    {6, "SetToolBoxTrapAddress/SetToolTrapAddress"},
  }, {}},
  "PtrZone", // 0x48 (called with flags as 0x148)
  "HPurge", // 0x49
  "HNoPurge", // 0x4A
  "SetGrowZone", // 0x4B
  "CompactMem", // 0x4C
  {"PurgeMem", {{4, "PurgeMemSys"}}, {}}, // 0x4D
  "AddDrive", // 0x4E
  "RDrvrInstall", // 0x4F
  "RelString/CompareString", // 0x50
  "ReadLocation/ReadXPRam", // 0x51
  "WriteLocation/WriteXPRam", // 0x52
  nullptr, // 0x53
  "UprString/UprText", // 0x54
  "StripAddress", // 0x55
  {"LwrString/LowerText", { // 0x56
    {2, "StripText"},
    {4, "UpperText"},
    {6, "StripUpperText"},
  }, {}},
  "SetAppBase/SetApplBase", // 0x57
  {"InsTime", {{4, "InsXTime"}}, {}}, // 0x58
  "RmvTime", // 0x59
  "PrimeTime", // 0x5A
  "PowerOff", // 0x5B
  "MemoryDispatch/MemoryDispatchA0Result", // 0x5C
  "SwapMMUMode", // 0x5D
  "NMInstall", // 0x5E
  "NMRemove", // 0x5F
  {"FSDispatch/HFSDispatch", {}, { // 0x60 (often but not always called with flags as 0x260)
    {0x0001, "PBOpenWD"},
    {0x0002, "PBCloseWD"},
    {0x0005, "PBCatMove"},
    {0x0006, "PBDirCreate"},
    {0x0007, "PBGetWDInfo"},
    {0x0008, "PBGetFCBInfo"},
    {0x0009, "PBGetCatInfo"},
    {0x000A, "PBSetCatInfo"},
    {0x000B, "PBSetVInfo"},
    {0x0010, "PBLockRange"},
    {0x0011, "PBUnlockRange"},
    {0x0014, "PBCreateFileIDRef"},
    {0x0015, "PBDeleteFileIDRef"},
    {0x0016, "PBResolveFileIDRef/LockRng"},
    {0x0017, "PBExchangeFiles/UnlockRng"},
    {0x0018, "PBCatSearch"},
    {0x001A, "PBHOpenDF"},
    {0x001B, "PBMakeFSSpec"},
    {0x0020, "PBDTGetPath"},
    {0x0021, "PBDTCloseDown"},
    {0x0022, "PBDTAddIcon"},
    {0x0023, "PBDTGetIcon"},
    {0x0024, "PBDTGetIconInfo"},
    {0x0025, "PBDTAddAPPL"},
    {0x0026, "PBDTRemoveAPPL"},
    {0x0027, "PBDTGetAPPL"},
    {0x0028, "PBDTSetComment"},
    {0x0029, "PBDTRemoveComment"},
    {0x002A, "PBDTGetComment"},
    {0x002B, "PBDTFlush"},
    {0x002C, "PBDTReset"},
    {0x002D, "PBDTGetInfo"},
    {0x002E, "PBDTOpenInform"},
    {0x002F, "PBDTDelete"},
    {0x0030, "PBHGetVolParms"},
    {0x0031, "PBHGetLogInInfo"},
    {0x0032, "PBHGetDirAccess"},
    {0x0033, "PBHSetDirAccess"},
    {0x0034, "PBHMapID"},
    {0x0035, "PBHMapName"},
    {0x0036, "PBHCopyFile"},
    {0x0037, "PBHMoveRename"},
    {0x0038, "PBHOpenDeny"},
    {0x0039, "PBHOpenRFDeny"},
    {0x003F, "PBGetVolMountInfoSize"},
    {0x0040, "PBGetVolMountInfo"},
    {0x0041, "PBVolumeMount"},
    {0x0042, "PBShare"},
    {0x0043, "PBUnshare"},
    {0x0044, "PBGetUGEntry"},
    {0x0060, "PBGetForeignPrivs"},
    {0x0061, "PBSetForeignPrivs"},
  }, 0x00FF},
  "MaxBlock", // 0x61
  {"PurgeSpace", {{5, "PurgeSpaceSys"}}, {}}, // 0x62
  "MaxApplZone", // 0x63
  "MoveHHi", // 0x64
  "StackSpace", // 0x65
  "NewEmptyHandle", // 0x66
  "HSetRBit", // 0x67
  "HClrRBit", // 0x68
  "HGetState", // 0x69
  "HSetState", // 0x6A
  "TestManager", // 0x6B
  "InitFS", // 0x6C
  "InitEvents", // 0x6D
  {"SlotManager", {}, { // 0x6E
    {0x0000, "SReadByte"},
    {0x0001, "SReadWord"},
    {0x0002, "SReadLong"},
    {0x0003, "SGetCString"},
    {0x0005, "SGetBlock"},
    {0x0006, "SFindStruct"},
    {0x0007, "SReadStruct"},
    {0x0008, "SVersion"},
    {0x0009, "SetSRsrcState"},
    {0x000A, "InsertSRTRec"},
    {0x000B, "SGetSRsrc"},
    {0x000C, "SGetTypeSRsrc"},
    {0x0010, "SReadInfo"},
    {0x0011, "SReadPRAMRec"},
    {0x0012, "SPutPRAMRec"},
    {0x0013, "SReadFHeader"},
    {0x0014, "SNextSRsrc"},
    {0x0015, "SNextTypeSRsrc"},
    {0x0016, "SRsrcInfo"},
    {0x0017, "SDisposEPtr"},
    {0x0018, "SCkCardStat"},
    {0x0019, "SReadDrvrName"},
    {0x001B, "SFindDevBase"},
    {0x001C, "SFindBigDevBase"},
    {0x001D, "SGetSRsrcPtr"},
    {0x0020, "InitSDeclMgr"},
    {0x0021, "SPrimaryInit"},
    {0x0022, "SCardChanged"},
    {0x0023, "SExec"},
    {0x0024, "SOffsetData"},
    {0x0025, "SInitPRAMRecs"},
    {0x0026, "SReadPBSize"},
    {0x0028, "SCalcStep"},
    {0x0029, "SInitSRsrcTable"},
    {0x002A, "SSearchSRT"},
    {0x002B, "SUpdateSRT"},
    {0x002C, "SCalcSPointer"},
    {0x002D, "SGetDriver"},
    {0x002E, "SPtrToSlot"},
    {0x002F, "SFindSInfoRecPtr"},
    {0x0030, "SFindSRsrcPtr"},
    {0x0031, "SDeleteSRTRec"},
  }},
  "SlotVInstall", // 0x6F
  "SlotVRemove", // 0x70
  "AttachVBL", // 0x71
  "DoVBLTask", // 0x72
  "OSReserved", // 0x73
  "CacheMgr", // 0x74
  "SIntInstall", // 0x75
  "SIntRemove", // 0x76
  "CountADBs", // 0x77
  "GetIndADB", // 0x78
  "GetADBInfo", // 0x79
  "SetADBInfo", // 0x7A
  "ADBReInit", // 0x7B
  "ADBOp", // 0x7C
  "GetDefaultStartup", // 0x7D
  "SetDefaultStartup", // 0x7E
  {"InternalWait", {}, { // 0x7F
    {0x0000, "SetTimeout"},
    {0x0001, "GetTimeout"},
  }},
  "GetVideoDefault", // 0x80
  "SetVideoDefault", // 0x81
  "DTInstall", // 0x82
  "SetOSDefault", // 0x83
  "GetOSDefault", // 0x84
  {"IdleUpdate/PMgrOp", { // 0x85 (use subs when flags&4, IdleUpdate otherwise)
    {4, {"IdleState", {}, {
      {0x0000, "EnableIdle"},
      {0x0001, "DisableIdle"},
      {0xFFFF, "GetCPUSpeed"},
    }}},
    {6, {"SerialPower", {}, {
      {0x0000, "BOn"},
      {0x0004, "AOn"},
      {0x0005, "AOnIgnoreModem"},
      {0xFF80, "BOff"},
      {0xFF84, "AOff"},
    }}},
  }, {}},
  "IOPInfoAccess", // 0x86
  "IOPMsgRequest", // 0x87
  "IOPMoveData", // 0x88
  "SCSIAtomic", // 0x89
  {"Sleep/SlpQInstall", {{2, "SleepQInstall"}, {4, "SleepQRemove/SlpQRemove"}}, {}}, // 0x8A
  "CommToolboxDispatch", // 0x8B
  "Wakeup", // 0x8C
  {"DebugUtil", {}, { // 0x8D
    {0x0000, "DebuggerGetMax"},
    {0x0001, "DebuggerEnter"},
    {0x0002, "DebuggerExit"},
    {0x0003, "DebuggerPoll"},
    {0x0004, "GetPageState"},
    {0x0005, "PageFaultFatal"},
    {0x0008, "EnterSupervisorMode"},
  }},
  "BTreeDispatch", // 0x8E
  "DeferUserFn", // 0x8F
  "SysEnvirons", // 0x90
  "Translate24To32", // 0x91
  "EgretDispatch", // 0x92
  "Microseconds", // 0x93
  "ServerDispatch", // 0x94
  "POGOMPW", // 0x95
  "SharedLibsMPW", // 0x96
  "FPPriv", // 0x97
  "HWPriv", // 0x98
  "XToolTable", // 0x99
  "vProcHelper", // 0x9A
  "Messager", // 0x9B
  "NewPtrStartup", // 0x9C
  "MoveHLow", // 0x9D
  "PowerMgrDispatch", // 0x9E
  "PowerDispatch", // 0x9F
  "vMRdAddr", // 0xA0
  "vMRdData", // 0xA1
  "vMWrData", // 0xA2
  nullptr, // 0xA3
  "HeapDispatch", // 0xA4
  "VisRegionChanged", // 0xA5
  "vStdEntry", // 0xA6
  "vStdExit", // 0xA7
  nullptr, // 0xA8
  nullptr, // 0xA9
  nullptr, // 0xAA
  nullptr, // 0xAB
  "FSMDispatch", // 0xAC
  {"Gestalt", {{3, "NewGestalt"}, {5, "ReplaceGestalt"}, {7, "GetGestaltProcPtr"}}, {}}, // 0xAD
  "vADBProc/VADBProc", // 0xAE
  "vMtCheck", // 0xAF
  "vCheckReMount", // 0xB0
  "vDtrmV2", // 0xB1
  "vFindDrive", // 0xB2
  "vFClose", // 0xB3
  "vFlushMDB", // 0xB4
  "vGoDriver", // 0xB5
  "vWaitUntil", // 0xB6
  "vSyncWait", // 0xB7
  "vSoundDead", // 0xB8
  "vDisptch", // 0xB9
  "vIAZInit", // 0xBA
  "vIAZPostInit", // 0xBB
  "vLaunchInit", // 0xBC
  "vCacheFlush", // 0xBD
  "vSysUtil", // 0xBE
  "vLg2Phys", // 0xBF
  "vFlushCache", // 0xC0
  "vGetBlock", // 0xC1
  "vMarkBlock", // 0xC2
  "vRelBlock", // 0xC3
  "vTrashBlocks", // 0xC4
  "vTrashVBlks", // 0xC5
  "vCacheWrIP", // 0xC6
  "vCacheRdIP", // 0xC7
  "vBasicIO", // 0xC8
  "vRdBlocks", // 0xC9
  "vWrBlocks", // 0xCA
  "vSetUpTags", // 0xCB
  "vBTClose", // 0xCC
  "vBTDelete", // 0xCD
  "vBTFlush", // 0xCE
  "vBTGetRecord", // 0xCF
  "vBTInsert", // 0xD0
  "vBTOpen", // 0xD1
  "vBTSearch", // 0xD2
  "vBTUpdate", // 0xD3
  "vGetNode", // 0xD4
  "vRelNode", // 0xD5
  "vAllocNode", // 0xD6
  "vFreeNode", // 0xD7
  "vExtBTFile", // 0xD8
  "vDeallocFile", // 0xD9
  "vExtendFile", // 0xDA
  "vTruncateFile", // 0xDB
  "vCMSetup", // 0xDC
  "PPC", // 0xDD
  "vDtrmV1", // 0xDE
  "vBlkAlloc", // 0xDF
  "vBlkDeAlloc", // 0xE0
  "vFileOpen", // 0xE1
  "vPermssnChk", // 0xE2
  "vFndFilName", // 0xE3
  "vRfNCall", // 0xE4
  "vAdjEOF", // 0xE5
  "vPixel2Char", // 0xE6
  "vChar2Pixel", // 0xE7
  "vHiliteText", // 0xE8
  "vFileClose", // 0xE9
  "vFileRead", // 0xEA
  "vFileWrite", // 0xEB
  "DispatchHelper", // 0xEC
  "vUpdAltMDB", // 0xED
  "vCkExtFS", // 0xEE
  "vDtrmV3", // 0xEF
  "vBMChk", // 0xF0
  "vTstMod", // 0xF1
  "vLocCRec", // 0xF2
  "vTreeSearch", // 0xF3
  "vMapFBlock", // 0xF4
  "vXFSearch", // 0xF5
  "vReadBM", // 0xF6
  "vDoEject", // 0xF7
  "vSegStack", // 0xF8
  "vSuperLoad", // 0xF9
  "vCmpFrm", // 0xFA
  "vNewMap", // 0xFB
  "vCheckLoad", // 0xFC
  "XTrimMeasure", // 0xFD
  "XFindWord/TEFindWord", // 0xFE
  "XFindLine/TEFindLine", // 0xFF
});

const vector<TrapInfo> toolbox_trap_info({
  {"SoundDispatch", {}, { // 0x800
    // TODO: this trap actually uses the high bits of D0 for the command and the
    // low bits for the MIDI tool number
    {0x0004, "MIDISignIn"},
    {0x0008, "MIDISignOut"},
    {0x000C, "MIDIGetClients"},
    {0x0010, "MIDIGetClientName"},
    {0x0014, "MIDISetClientName"},
    {0x0018, "MIDIGetPorts"},
    {0x001C, "MIDIAddPort"},
    {0x0020, "MIDIGetPortInfo"},
    {0x0024, "MIDIConnectData"},
    {0x0028, "MIDIUnConnectData"},
    {0x002C, "MIDIConnectTime"},
    {0x0030, "MIDIUnConnectTime"},
    {0x0034, "MIDIFlush"},
    {0x0038, "MIDIGetReadHook"},
    {0x003C, "MIDISetReadHook"},
    {0x0040, "MIDIGetPortName"},
    {0x0044, "MIDISetPortName"},
    {0x0048, "MIDIWakeUp"},
    {0x004C, "MIDIRemovePort"},
    {0x0050, "MIDIGetSync"},
    {0x0054, "MIDISetSync"},
    {0x0058, "MIDIGetCurTime"},
    {0x005C, "MIDISetCurTime"},
    {0x0060, "MIDIStartTime"},
    {0x0064, "MIDIStopTime"},
    {0x0068, "MIDIPoll"},
    {0x006C, "MIDIWritePacket"},
    {0x0070, "MIDIWorldChanged"},
    {0x0074, "MIDIGetOffsetTime"},
    {0x0078, "MIDISetOffsetTime"},
    {0x007C, "MIDIConvertTime"},
    {0x0080, "MIDIGetRefCon"},
    {0x0084, "MIDISetRefCon"},
    {0x0088, "MIDIGetClRefCon"},
    {0x008C, "MIDISetClRefCon"},
    {0x0090, "MIDIGetTCFormat"},
    {0x0094, "MIDISetTCFormat"},
    {0x0098, "MIDISetRunRate"},
    {0x009C, "MIDIGetClientIcon"},
  }},
  "SndDisposeChannel", // 0x801
  "SndAddModifier", // 0x802
  "SndDoCommand", // 0x803
  "SndDoImmediate", // 0x804
  "SndPlay", // 0x805
  "SndControl", // 0x806
  "SndNewChannel", // 0x807
  "InitProcMenu", // 0x808
  "GetControlVariant/GetCVariant", // 0x809
  "GetWVariant", // 0x80A
  "PopUpMenuSelect", // 0x80B
  "RGetResource", // 0x80C
  "Count1Resources", // 0x80D
  "Get1IndResource/Get1IxResource", // 0x80E
  "Get1IndType/Get1IxType", // 0x80F
  "Unique1ID", // 0x810
  "TESelView", // 0x811
  "TEPinScroll", // 0x812
  "TEAutoView", // 0x813
  "SetFractEnable", // 0x814
  {"SCSIDispatch", {}, { // 0x815
    {0x0000, "SCSIReset"},
    {0x0001, "SCSIGet"},
    {0x0002, "SCSISelect"},
    {0x0003, "SCSICmd"},
    {0x0004, "SCSIComplete"},
    {0x0005, "SCSIRead"},
    {0x0006, "SCSIWrite"},
    {0x0007, "SCSIInstall"},
    {0x0008, "SCSIRBlind"},
    {0x0009, "SCSIWBlind"},
    {0x000A, "SCSIStat"},
    {0x000B, "SCSISelAtn"},
    {0x000C, "SCSIMsgIn"},
    {0x000D, "SCSIMsgOut"},
  }},
  {"Pack8", {}, { // 0x816
    {0x011E, "AESetInteractionAllowed"},
    {0x0204, "AEDisposeDesc"},
    {0x0219, "AEResetTimer"},
    {0x021A, "AEGetTheCurrentEvent"},
    {0x021B, "AEProcessAppleEvent"},
    {0x021D, "AEGetInteractionAllowed"},
    {0x022B, "AESuspendTheCurrentEvent"},
    {0x022C, "AESetTheCurrentEvent"},
    {0x0405, "AEDuplicateDesc"},
    {0x0407, "AECountItems"},
    {0x040E, "AEDeleteItem"},
    {0x0413, "AEDeleteKeyDesc"},
    {0x0413, "AEDeleteParam"},
    {0x0500, "AEInstallSpecialHandler"},
    {0x0501, "AERemoveSpecialHandler"},
    {0x052D, "AEGetSpecialHandler"},
    {0x0603, "AECoerceDesc"},
    {0x0609, "AEPutDesc"},
    {0x0610, "AEPutKeyDesc"},
    {0x0610, "AEPutParamDesc"},
    {0x061C, "AEInteractWithUser"},
    {0x0627, "AEPutAttributeDesc"},
    {0x0706, "AECreateList"},
    {0x0720, "AERemoveEventHandler"},
    {0x0723, "AERemoveCoercionHandler"},
    {0x0812, "AEGetKeyDesc"},
    {0x0812, "AEGetParamDesc"},
    {0x0818, "AEResumeTheCurrentEvent"},
    {0x0825, "AECreateDesc"},
    {0x0826, "AEGetAttributeDesc"},
    {0x0828, "AESizeOfAttribute"},
    {0x0829, "AESizeOfKeyDesc"},
    {0x0829, "AESizeOfParam"},
    {0x082A, "AESizeOfNthItem"},
    {0x091F, "AEInstallEventHandler"},
    {0x0921, "AEGetEventHandler"},
    {0x0A02, "AECoercePtr"},
    {0x0A08, "AEPutPtr"},
    {0x0A0B, "AEGetNthDesc"},
    {0x0A0F, "AEPutKeyPtr"},
    {0x0A0F, "AEPutParamPtr"},
    {0x0A16, "AEPutAttributePtr"},
    {0x0A22, "AEInstallCoercionHandler"},
    {0x0B0D, "AEPutArray"},
    {0x0B14, "AECreateAppleEvent"},
    {0x0B24, "AEGetCoercionHandler"},
    {0x0D0C, "AEGetArray"},
    {0x0D17, "AESend"},
    {0x0E11, "AEGetKeyPtr"},
    {0x0E11, "AEGetParamPtr"},
    {0x0E15, "AEGetAttributePtr"},
    {0x100A, "AEGetNthPtr"},
  }},
  "CopyMask", // 0x817
  "FixATan2", // 0x818
  "XMunger", // 0x819
  "HOpenResFile", // 0x81A
  "HCreateResFile", // 0x81B
  "Count1Types", // 0x81C
  "InvalMenuBar", // 0x81D
  "SaveRestoreBits", // 0x81E
  "Get1Resource", // 0x81F
  "Get1NamedResource", // 0x820
  "MaxSizeRsrc", // 0x821
  {"ResourceDispatch", {}, { // 0x822
    {0x0001, "ReadPartialResource"},
    {0x0002, "WritePartialResource"},
    {0x0003, "SetResourceSize"},
    {0x000A, "GetNextFOND"},
  }},
  {"AliasDispatch", {}, { // 0x823
    {0x0000, "FindFolder"},
    {0x0002, "NewAlias"},
    {0x0003, "ResolveAlias"},
    {0x0005, "MatchAlias"},
    {0x0006, "UpdateAlias"},
    {0x0007, "GetAliasInfo"},
    {0x0008, "NewAliasMinimal"},
    {0x0009, "NewAliasMinimalFromFullPath"},
    {0x000C, "ResolveAliasFile"},
  }},
  "HFSUtilDispatch/FSMgr", // 0x824
  {"MenuDispatch", {}, { // 0x825
    {0x0400, "InsertFontResMenu"},
    {0x0601, "InsertIntlResMenu"},
  }},
  "InsertMenuItem/InsMenuItem", // 0x826
  "HideDialogItem/HideDItem", // 0x827
  "ShowDialogItem/ShowDItem", // 0x828
  "LayerDispatch", // 0x829
  {"ComponentDispatch", {}, { // 0x82A
    {0x0000, {"__component_multi__", {}, {
      {0xFFFFFFFA, "ComponentSetTarget"},
      {0xFFFFFFFC, "GetComponentVersion"},
      {0xFFFFFFFD, "ComponentFunctionImplemented"},
      {0x00000002, "InitiateTextService"},
      {0x00000003, "TerminateTextService"},
      {0x00000004, "ActivateTextService"},
      {0x00000005, "DeactivateTextService"},
      {0x00000006, "TextServiceEvent"},
      {0x00000007, "GetTextServiceMenu"},
      {0x00000008, "TextServiceMenuSelect"},
      {0x00000009, "FixTextService"},
      {0x0000000A, "SetTextServiceCursor"},
      {0x0000000B, "HidePaletteWindows"},
      {0x04000001, "GetScriptLanguageSupport"},
    }}},
    {0x0001, "RegisterComponent"},
    {0x0002, "UnregisterComponent"},
    {0x0003, "CountComponents"},
    {0x0004, "FindNextComponent"},
    {0x0005, "GetComponentInfo"},
    {0x0006, "GetComponentListModSeed"},
    {0x0007, "OpenComponent"},
    {0x0008, "CloseComponent"},
    {0x000A, "GetComponentInstanceError"},
    {0x000B, "SetComponentInstanceError"},
    {0x000C, "GetComponentInstanceStorage"},
    {0x000D, "SetComponentInstanceStorage"},
    {0x000E, "GetComponentInstanceA5"},
    {0x000F, "SetComponentInstanceA5"},
    {0x0010, "GetComponentRefcon"},
    {0x0011, "SetComponentRefcon"},
    {0x0012, "RegisterComponentResource"},
    {0x0013, "CountComponentInstances"},
    {0x0014, "RegisterComponentResourceFile"},
    {0x0015, "OpenComponentResFile"},
    {0x0018, "CloseComponentResFile"},
    {0x001C, "CaptureComponent"},
    {0x001D, "UncaptureComponent"},
    {0x001E, "SetDefaultComponent"},
    {0x0021, "OpenDefaultComponent"},
    {0x0024, "DelegateComponentCall"},
    {-1, "CallComponentFunction/CallComponentFunctionWithStorage"},
  }},
  {"Pack9", {}, { // 0x82B
    {0x0D00, "PPCBrowser"},
  }},
  "Pack10", // 0x82C
  {"Pack11", {}, { // 0x82D
    // Note: InitEditionPack requires pushing 0x0011 to the stack also
    {0x0100, "InitEditionPack"},
    {0x0A02, "NewSection"},
    {0x0604, "RegisterSection"},
    {0x0206, "UnRegisterSection"},
    {0x0208, "IsRegisteredSection"},
    {0x040C, "AssociateSection"},
    {0x050E, "CreateEditionContainerFile"},
    {0x0210, "DeleteEditionContainerFile"},
    {0x0412, "OpenEdition"},
    {0x0814, "OpenNewEdition"},
    {0x0316, "CloseEdition"},
    {0x0618, "EditionHasFormat"},
    {0x081A, "ReadEdition"},
    {0x081C, "WriteEdition"},
    {0x061E, "GetEditionFormatMark"},
    {0x0620, "SetEditionFormatMark"},
    {0x0422, "GetEditionInfo"},
    {0x0224, "GoToPublisherSection"},
    {0x0226, "GetLastEditionContainerUsed"},
    {0x0A28, "GetStandardFormats"},
    {0x022A, "GetEditionOpenerProc"},
    {0x022C, "SetEditionOpenerProc"},
    {0x052E, "CallEditionOpenerProc"},
    {0x0530, "CallFormatIOProc"},
    {0x0232, "NewSubscriberDialog"},
    {0x0B34, "NewSubscriberExpDialog"},
    {0x0236, "NewPublisherDialog"},
    {0x0B38, "NewPublisherExpDialog"},
    {0x023A, "SectionOptionsDialog"},
    {0x0B3C, "SectionOptionsExpDialog"},
  }},
  {"Pack12", {}, { // 0x82E
    {0x0001, "Fix2SmallFract"},
    {0x0002, "SmallFract2Fix"},
    {0x0003, "CMY2RGB"},
    {0x0004, "RGB2CMY"},
    {0x0005, "HSL2RGB"},
    {0x0006, "RGB2HSL"},
    {0x0007, "HSV2RGB"},
    {0x0008, "RGB2HSV"},
    {0x0009, "GetColor"},
  }},
  {"Pack13", {}, { // 0x82F
    // Note: InitDBPack seems to require pushing 0004 onto the stack first
    {0x0100, "InitDBPack"},
    {0x020E, "DBKill"},
    {0x0210, "DBDisposeQuery"},
    {0x0215, "DBRemoveResultHandler"},
    {0x030F, "DBGetNewQuery"},
    {0x0403, "DBEnd"},
    {0x0408, "DBExec"},
    {0x0409, "DBState"},
    {0x040D, "DBUnGetItem"},
    {0x0413, "DBResultsToText"},
    {0x050B, "DBBreak"},
    {0x0514, "DBInstallResultHandler"},
    {0x0516, "DBGetResultHandler"},
    {0x0605, "DBGetSessionNum"},
    {0x0706, "DBSend"},
    {0x0811, "DBStartQuery"},
    {0x0A12, "DBGetQueryResults"},
    {0x0B07, "DBSendItem"},
    {0x0E02, "DBInit"},
    {0x0E0A, "DBGetErr"},
    {0x100C, "DBGetItem"},
    {0x1704, "DBGetConnInfo"},
  }},
  {"Pack14", {}, { // 0x830
    {0x0002, "HMRemoveBalloon"},
    {0x0003, "HMGetBalloons"},
    {0x0007, "HMIsBalloon"},
    {0x0104, "HMSetBalloons"},
    {0x0108, "HMSetFont"},
    {0x0109, "HMSetFontSize"},
    {0x010C, "HMSetDialogResID"},
    {0x0200, "HMGetHelpMenuHandle"},
    {0x020A, "HMGetFont"},
    {0x020B, "HMGetFontSize"},
    {0x020D, "HMSetMenuResID"},
    {0x0213, "HMGetDialogResID"},
    {0x0215, "HMGetBalloonWindow"},
    {0x0314, "HMGetMenuResID"},
    {0x040E, "HMBalloonRect"},
    {0x040F, "HMBalloonPict"},
    {0x0410, "HMScanTemplateItems"},
    {0x0711, "HMExtractHelpMsg"},
    {0x0B01, "HMShowBalloon"},
    {0x0E05, "HMShowMenuBalloon"},
    {0x1306, "HMGetIndHelpMsg"},
  }},
  {"Pack15", {}, { // 0x831
    {0x0800, "GetPictInfo"},
    {0x0801, "GetPixMapInfo"},
    {0x0602, "NewPictInfo"},
    {0x0403, "RecordPictInfo"},
    {0x0404, "RecordPixMapInfo"},
    {0x0505, "RetrievePictInfo"},
    {0x0206, "DisposPictInfo"},
  }},
  "QuickDrawGX", // 0x832
  "ScrnBitMap", // 0x833
  "SetFScaleDisable", // 0x834
  "FontMetrics", // 0x835
  "GetMaskTable", // 0x836
  "MeasureText", // 0x837
  "CalcMask", // 0x838
  "SeedFill", // 0x839
  "ZoomWindow", // 0x83A
  "TrackBox", // 0x83B
  "TEGetOffset", // 0x83C
  {"TEDispatch", {}, { // 0x83D
    {0x0000, "TEStylePaste/TEStylPaste"},
    {0x0001, "TESetStyle"},
    {0x0002, "TEReplaceStyle"},
    {0x0003, "TEGetStyle"},
    {0x0004, "GetStyleHandle/GetStylHandle/TEGetStyleHandle"},
    {0x0005, "SetStyleHandle/SetStylHandle/TESetStyleHandle"},
    {0x0006, "GetStyleScrap/GetStylScrap/TEGetStyleScrapHandle"},
    {0x0007, "TEStyleInsert/TEStylInsert"},
    {0x0008, "TEGetPoint"},
    {0x0009, "TEGetHeight"},
    {0x000A, "TEContinuousStyle"},
    {0x000B, "SetStyleScrap/SetStylScrap/TEUseStyleScrap"},
    {0x000C, "TECustomHook"},
    {0x000D, "TENumStyles"},
    {0x000E, "TEFeatureFlag"},
  }},
  "TEStyleNew", // 0x83E
  "Long2Fix", // 0x83F
  "Fix2Long", // 0x840
  "Fix2Frac", // 0x841
  "Frac2Fix", // 0x842
  "Fix2X", // 0x843
  "X2Fix", // 0x844
  "Frac2X", // 0x845
  "X2Frac", // 0x846
  "FracCos", // 0x847
  "FracSin", // 0x848
  "FracSqrt", // 0x849
  "FracMul", // 0x84A
  "FracDiv", // 0x84B
  "UserDelay", // 0x84C
  "FixDiv", // 0x84D
  "GetItemCmd", // 0x84E
  "SetItemCmd", // 0x84F
  "InitCursor", // 0x850
  "SetCursor", // 0x851
  "HideCursor", // 0x852
  "ShowCursor", // 0x853
  {"FontDispatch", {}, { // 0x854
    {0x0000, "IsOutline"},
    {0x0001, "SetOutlinePreferred"},
    {0x0009, "GetOutlinePreferred"},
    {0x0008, "OutlineMetrics"},
    {0x000A, "SetPreserveGlyph"},
    {0x000B, "GetPreserveGlyph"},
    {0x000C, "FlushFonts"},
  }},
  "ShieldCursor", // 0x855
  "ObscureCursor", // 0x856
  "SetEntry", // 0x857
  "BitAnd", // 0x858
  "BitXor", // 0x859
  "BitNot", // 0x85A
  "BitOr", // 0x85B
  "BitShift", // 0x85C
  "BitTst", // 0x85D
  "BitSet", // 0x85E
  "BitClr", // 0x85F
  "WaitNextEvent", // 0x860
  "Random", // 0x861
  "ForeColor", // 0x862
  "BackColor", // 0x863
  "ColorBit", // 0x864
  "GetPixel", // 0x865
  "StuffHex", // 0x866
  "LongMul", // 0x867
  "FixMul", // 0x868
  "FixRatio", // 0x869
  "HiWord", // 0x86A
  "LoWord", // 0x86B
  "FixRound", // 0x86C
  "InitPort", // 0x86D
  "InitGraf", // 0x86E
  "OpenPort", // 0x86F
  "LocalToGlobal", // 0x870
  "GlobalToLocal", // 0x871
  "GrafDevice", // 0x872
  "SetPort", // 0x873
  "GetPort", // 0x874
  "SetPBits/SetPortBits", // 0x875
  "PortSize", // 0x876
  "MovePortTo", // 0x877
  "SetOrigin", // 0x878
  "SetClip", // 0x879
  "GetClip", // 0x87A
  "ClipRect", // 0x87B
  "BackPat", // 0x87C
  "ClosePort", // 0x87D
  "AddPt", // 0x87E
  "SubPt", // 0x87F
  "SetPt", // 0x880
  "EqualPt", // 0x881
  "StdText", // 0x882
  "DrawChar", // 0x883
  "DrawString", // 0x884
  "DrawText", // 0x885
  "TextWidth", // 0x886
  "TextFont", // 0x887
  "TextFace", // 0x888
  "TextMode", // 0x889
  "TextSize", // 0x88A
  "GetFontInfo", // 0x88B
  "StringWidth", // 0x88C
  "CharWidth", // 0x88D
  "SpaceExtra", // 0x88E
  {"OSDispatch", {}, { // 0x88F
    {0x0015, "MFMaxMem/TempMaxMem"},
    {0x0016, "MFTopMem/TempTopMem"},
    {0x0018, "MFFreeMem/TempFreeMem"},
    {0x001D, "MFTempNewHandle/TempNewHandle"},
    {0x001E, "MFTempHLock/TempHLock"},
    {0x001F, "MFTempHUnlock/TempHUnlock"},
    {0x0020, "MFTempDisposHandle/TempDisposeHandle"},
    {0x0033, "AcceptHighLevelEvent"},
    {0x0034, "PostHighLevelEvent"},
    {0x0035, "GetProcessSerialNumberFromPortName"},
    {0x0036, "LaunchDeskAccessory"},
    {0x0037, "GetCurrentProcess"},
    {0x0038, "GetNextProcess"},
    {0x0039, "GetFrontProcess"}, // looks like the argument to this should always be -1?
    {0x003A, "GetProcessInformation"},
    {0x003B, "SetFrontProcess"},
    {0x003C, "WakeUpProcess"},
    {0x003D, "SameProcess"},
    {0x0045, "GetSpecificHighLevelEvent"},
    {0x0046, "GetPortNameFromProcessSerialNumber"},
  }},
  "StdLine", // 0x890
  "LineTo", // 0x891
  "Line", // 0x892
  "MoveTo", // 0x893
  "Move", // 0x894
  {"ShutDown", {}, { // 0x895
    {0x0001, "ShutDwnPower"},
    {0x0002, "ShutDwnStart"},
    {0x0003, "ShutDwnInstall"},
    {0x0004, "ShutDwnRemove"},
  }},
  "HidePen", // 0x896
  "ShowPen", // 0x897
  "GetPenState", // 0x898
  "SetPenState", // 0x899
  "GetPen", // 0x89A
  "PenSize", // 0x89B
  "PenMode", // 0x89C
  "PenPat", // 0x89D
  "PenNormal", // 0x89E
  // 89F is also named EnableDogCow, DisableDogCow, InitDogCow, and Moof in some
  // trap lists (e.g. Executor, Basilisk II). There don't appear to be any other
  // references to these names online anywhere though.
  "Unimplemented", // 0x89F
  "StdRect", // 0x8A0
  "FrameRect", // 0x8A1
  "PaintRect", // 0x8A2
  "EraseRect", // 0x8A3
  "InverRect", // 0x8A4
  "FillRect", // 0x8A5
  "EqualRect", // 0x8A6
  "SetRect", // 0x8A7
  "OffsetRect", // 0x8A8
  "InsetRect", // 0x8A9
  "SectRect", // 0x8AA
  "UnionRect", // 0x8AB
  "Pt2Rect", // 0x8AC
  "PtInRect", // 0x8AD
  "EmptyRect", // 0x8AE
  "StdRRect", // 0x8AF
  "FrameRoundRect", // 0x8B0
  "PaintRoundRect", // 0x8B1
  "EraseRoundRect", // 0x8B2
  "InverRoundRect", // 0x8B3
  "FillRoundRect", // 0x8B4
  {"ScriptUtil", {}, { // 0x8B5
    {0x0000, "FontScript/smFontScript"},
    {0x0002, "IntlScript/smIntlScript"},
    {0x0004, "KeyScript/smKybdScript"},
    {0x0006, "Font2Script/FontToScript/smFont2Script"},
    {0x0008, "GetEnvirons/GetScriptManagerVariable/smGetEnvirons"},
    {0x000A, "SetEnvirons/SetScriptManagerVariable/smSetEnvirons"},
    {0x000C, "GetScript/GetScriptVariable/smGetScript"},
    {0x000E, "SetScript/SetScriptVariable/smSetScript"},
    {0x0010, "CharacterByteType/CharByte/smCharByte"},
    {0x0012, "CharacterType/CharType/smCharType"},
    {0x0014, "Pixel2Char/smPixel2Char"},
    {0x0016, "Char2Pixel/smChar2Pixel"},
    {0x0018, "Transliterate/TransliterateText/smTranslit"},
    {0x001A, "FindWord/FindWordBreaks/smFindWord"},
    {0x001C, "HiliteText/smHiliteText"},
    {0x001E, "DrawJust/smDrawJust"},
    {0x0020, "MeasureJust/smMeasureJust"},
    {0x0022, "FillParseTable/ParseTable"},
    {0x0024, "PortionText"},
    {0x0026, "FindScriptRun"},
    {0x0028, "VisibleLength"},
    {0x002E, "NPixel2Char/PixelToChar"},
    {0x0030, "CharToPixel/NChar2Pixel"},
    {0x0032, "DrawJustified/NDrawJust"},
    {0x0034, "MeasureJustified/NMeasureJust"},
    {0x0036, "NPortionText/PortionLine"},
    {0x0038, "GetScriptUtilityAddress"},
    {0x003A, "SetScriptUtilityAddress"},
    {0x003C, "GetScriptQDPatchAddress"},
    {0x003E, "SetScriptQDPatchAddress"},
    {0xFFB6, {"__text_macro__", {}, {
      {0x0000, "LowercaseText"},
      {0x0200, "StripDiacritics"},
      {0x0400, "UppercaseText"},
      {0x0600, "UppercaseStripDiacritics"},
    }}},
    {0xFFDC, "ReplaceText"},
    {0xFFDE, "TruncText"},
    {0xFFE0, "TruncString"},
    {0xFFE2, "NFindWord"},
    {0xFFE4, "ValidDate"},
    {0xFFE6, "FormatStr2X/StringToExtended"},
    {0xFFE8, "FormatX2Str/ExtendedToString"},
    {0xFFEA, "Format2Str/FormatRecToString"},
    {0xFFEC, "Str2Format/StringToFormatRec"},
    {0xFFEE, "ToggleDate"},
    {0xFFF0, "LongSecondsToDate/LongSecs2Date"},
    {0xFFF2, "LongDate2Secs/LongDateToSeconds"},
    {0xFFF4, "String2Time/StringToTime"},
    {0xFFF6, "String2Date/StringToDate"},
    {0xFFF8, "InitDateCache"},
    {0xFFFA, "IntlTokenize"},
    {0xFFFC, "GetFormatOrder"},
    {0xFFFE, "StyledLineBreak"},
  }, 0x0000FFFF},
  "StdOval", // 0x8B6
  "FrameOval", // 0x8B7
  "PaintOval", // 0x8B8
  "EraseOval", // 0x8B9
  "InvertOval", // 0x8BA
  "FillOval", // 0x8BB
  "SlopeFromAngle", // 0x8BC
  "StdArc", // 0x8BD
  "FrameArc", // 0x8BE
  "PaintArc", // 0x8BF
  "EraseArc", // 0x8C0
  "InvertArc", // 0x8C1
  "FillArc", // 0x8C2
  "PtToAngle", // 0x8C3
  "AngleFromSlope", // 0x8C4
  "StdPoly", // 0x8C5
  "FramePoly", // 0x8C6
  "PaintPoly", // 0x8C7
  "ErasePoly", // 0x8C8
  "InvertPoly", // 0x8C9
  "FillPoly", // 0x8CA
  "OpenPoly", // 0x8CB
  "ClosePoly/ClosePgon", // 0x8CC
  "KillPoly", // 0x8CD
  "OffsetPoly", // 0x8CE
  "PackBits", // 0x8CF
  "UnpackBits", // 0x8D0
  "StdRgn", // 0x8D1
  "FrameRgn", // 0x8D2
  "PaintRgn", // 0x8D3
  "EraseRgn", // 0x8D4
  "InverRgn/InvertRgn", // 0x8D5
  "FillRgn", // 0x8D6
  "BitMapToRegion/BitMapRgn", // 0x8D7
  "NewRgn", // 0x8D8
  "DisposRgn/DisposeRgn", // 0x8D9
  "OpenRgn", // 0x8DA
  "CloseRgn", // 0x8DB
  "CopyRgn", // 0x8DC
  "SetEmptyRgn", // 0x8DD
  "SetRecRgn", // 0x8DE
  "RectRgn", // 0x8DF
  "OffsetRgn/OfsetRgn", // 0x8E0
  "InsetRgn", // 0x8E1
  "EmptyRgn", // 0x8E2
  "EqualRgn", // 0x8E3
  "SectRgn", // 0x8E4
  "UnionRgn", // 0x8E5
  "DiffRgn", // 0x8E6
  "XorRgn", // 0x8E7
  "PtInRgn", // 0x8E8
  "RectInRgn", // 0x8E9
  "SetStdProcs", // 0x8EA
  "StdBits", // 0x8EB
  "CopyBits", // 0x8EC
  "StdTxMeas", // 0x8ED
  "StdGetPic", // 0x8EE
  "ScrollRect", // 0x8EF
  "StdPutPic", // 0x8F0
  "StdComment", // 0x8F1
  "PicComment", // 0x8F2
  "OpenPicture", // 0x8F3
  "ClosePicture", // 0x8F4
  "KillPicture", // 0x8F5
  "DrawPicture", // 0x8F6
  "Layout", // 0x8F7
  "ScalePt", // 0x8F8
  "MapPt", // 0x8F9
  "MapRect", // 0x8FA
  "MapRgn", // 0x8FB
  "MapPoly", // 0x8FC
  {"PrGlue", {}, { // 0x8FD
    {0x04000C00, "PrOpenDoc"},
    {0x08000484, "PrCloseDoc"},
    {0x10000808, "PrOpenPage"},
    {0x1800040C, "PrClosePage"},
    {0x20040480, "PrintDefault"},
    {0x2A040484, "PrStlDialog"},
    {0x32040488, "PrJobDialog"},
    {0x3C04040C, "PrStlInit"},
    {0x44040410, "PrJobInit"},
    {0x4A040894, "PrDlgMain"},
    {0x52040498, "PrValidate"},
    {0x5804089C, "PrJobMerge"},
    {0x60051480, "PrPicFile"},
    {0x70070480, "PrGeneral"},
    {0x80000000, "PrDrvrOpen"},
    {0x88000000, "PrDrvrClose"},
    {0x94000000, "PrDrvrDCE"},
    {0x9A000000, "PrDrvrVers"},
    {0xA0000E00, "PrCtlCall"},
    {0xA8000000, "PrPurge"},
    {0xB0000000, "PrNoPurge"},
    {0xBA000000, "PrError"},
    {0xC0000200, "PrSetError"},
    {0xC8000000, "PrOpen"},
    {0xD0000000, "PrClose"},
  }},
  "InitFonts", // 0x8FE
  "GetFName/GetFontName", // 0x8FF
  "GetFNum", // 0x900
  "FMSwapFont", // 0x901
  "RealFont", // 0x902
  "SetFontLock", // 0x903
  "DrawGrowIcon", // 0x904
  "DragGrayRgn", // 0x905
  "NewString", // 0x906
  "SetString", // 0x907
  "ShowHide", // 0x908
  "CalcVis", // 0x909
  "CalcVBehind", // 0x90A
  "ClipAbove", // 0x90B
  "PaintOne", // 0x90C
  "PaintBehind", // 0x90D
  "SaveOld", // 0x90E
  "DrawNew", // 0x90F
  "GetWMgrPort", // 0x910
  "CheckUpdate", // 0x911
  "InitWindows", // 0x912
  "NewWindow", // 0x913
  "DisposeWindow", // 0x914
  "ShowWindow", // 0x915
  "HideWindow", // 0x916
  "GetWRefCon", // 0x917
  "SetWRefCon", // 0x918
  "GetWTitle", // 0x919
  "SetWTitle", // 0x91A
  "MoveWindow", // 0x91B
  "HiliteWindow", // 0x91C
  "SizeWindow", // 0x91D
  "TrackGoAway", // 0x91E
  "SelectWindow", // 0x91F
  "BringToFront", // 0x920
  "SendBehind", // 0x921
  "BeginUpdate", // 0x922
  "EndUpdate", // 0x923
  "FrontWindow", // 0x924
  "DragWindow", // 0x925
  "DragTheRgn", // 0x926
  "InvalRgn", // 0x927
  "InvalRect", // 0x928
  "ValidRgn", // 0x929
  "ValidRect", // 0x92A
  "GrowWindow", // 0x92B
  "FindWindow", // 0x92C
  "CloseWindow", // 0x92D
  "SetWindowPic", // 0x92E
  "GetWindowPic", // 0x92F
  "InitMenus", // 0x930
  "NewMenu", // 0x931
  "DisposeMenu", // 0x932
  "AppendMenu", // 0x933
  "ClearMenuBar", // 0x934
  "InsertMenu", // 0x935
  "DeleteMenu", // 0x936
  "DrawMenuBar", // 0x937
  "HiliteMenu", // 0x938
  "EnableItem", // 0x939
  "DisableItem", // 0x93A
  "GetMenuBar", // 0x93B
  "SetMenuBar", // 0x93C
  "MenuSelect", // 0x93D
  "MenuKey", // 0x93E
  "GetItmIcon", // 0x93F
  "SetItmIcon", // 0x940
  "GetItmStyle", // 0x941
  "SetItmStyle", // 0x942
  "GetItmMark", // 0x943
  "SetItmMark", // 0x944
  "CheckItem", // 0x945
  "GetMenuItemText/GetItem", // 0x946
  "SetMenuItemText/SetItem", // 0x947
  "CalcMenuSize", // 0x948
  "GetMenuHandle", // 0x949
  "SetMFlash", // 0x94A
  "PlotIcon", // 0x94B
  "FlashMenuBar", // 0x94C
  "AppendResMenu/AddResMenu", // 0x94D
  "PinRect", // 0x94E
  "DeltaPoint", // 0x94F
  "CountMItems", // 0x950
  "InsertResMenu", // 0x951
  "DeleteMenuItem/DelMenuItem", // 0x952
  "UpdtControl", // 0x953
  "NewControl", // 0x954
  "DisposeControl", // 0x955
  "KillControls", // 0x956
  "ShowControl", // 0x957
  "HideControl", // 0x958
  "MoveControl", // 0x959
  "GetControlReference/GetCRefCon", // 0x95A
  "SetControlReference/SetCRefCon", // 0x95B
  "SizeControl", // 0x95C
  "HiliteControl", // 0x95D
  "GetControlTitle/GetCTitle", // 0x95E
  "SetControlTitle/SetCTitle", // 0x95F
  "GetControlValue/GetCtlValue", // 0x960
  "GetControlMinimum/GetMinCtl", // 0x961
  "GetControlMaximum/GetMaxCtl", // 0x962
  "SetControlValue/SetCtlValue", // 0x963
  "SetControlMinimum/SetMinCtl", // 0x964
  "SetControlMaximum/SetMaxCtl", // 0x965
  "TestControl", // 0x966
  "DragControl", // 0x967
  "TrackControl", // 0x968
  "DrawControls", // 0x969
  "GetControlAction/GetCtlAction", // 0x96A
  "SetControlAction/SetCtlAction", // 0x96B
  "FindControl", // 0x96C
  "Draw1Control", // 0x96D
  "Dequeue", // 0x96E
  "Enqueue", // 0x96F
  "GetNextEvent", // 0x970
  "EventAvail", // 0x971
  "GetMouse", // 0x972
  "StillDown", // 0x973
  "Button", // 0x974
  "TickCount", // 0x975
  "GetKeys", // 0x976
  "WaitMouseUp", // 0x977
  "UpdtDialog", // 0x978
  "CouldDialog", // 0x979
  "FreeDialog", // 0x97A
  "InitDialogs", // 0x97B
  "GetNewDialog", // 0x97C
  "NewDialog", // 0x97D
  "SelectDialogItemText/SelIText", // 0x97E
  "IsDialogEvent", // 0x97F
  "DialogSelect", // 0x980
  "DrawDialog", // 0x981
  "CloseDialog", // 0x982
  "DisposeDialog", // 0x983
  "FindDialogItem/FindDItem", // 0x984
  "Alert", // 0x985
  "StopAlert", // 0x986
  "NoteAlert", // 0x987
  "CautionAlert", // 0x988
  "CouldAlert", // 0x989
  "FreeAlert", // 0x98A
  "ParamText", // 0x98B
  "ErrorSound", // 0x98C
  "GetDialogItem/GetDItem", // 0x98D
  "SetDialogItem/SetDItem", // 0x98E
  "SetDialogItemText/SetIText", // 0x98F
  "GetDialogItemText/GetIText", // 0x990
  "ModalDialog", // 0x991
  "DetachResource", // 0x992
  "SetResPurge", // 0x993
  "CurResFile", // 0x994
  "InitResources", // 0x995
  "RsrcZoneInit", // 0x996
  "OpenResFile", // 0x997
  "UseResFile", // 0x998
  "UpdateResFile", // 0x999
  "CloseResFile", // 0x99A
  "SetResLoad", // 0x99B
  "CountResources", // 0x99C
  "GetIndResource", // 0x99D
  "CountTypes", // 0x99E
  "GetIndType", // 0x99F
  "GetResource", // 0x9A0
  "GetNamedResource", // 0x9A1
  "LoadResource", // 0x9A2
  "ReleaseResource", // 0x9A3
  "HomeResFile", // 0x9A4
  "SizeRsrc", // 0x9A5
  "GetResAttrs", // 0x9A6
  "SetResAttrs", // 0x9A7
  "GetResInfo", // 0x9A8
  "SetResInfo", // 0x9A9
  "ChangedResource", // 0x9AA
  "AddResource", // 0x9AB
  "AddReference", // 0x9AC
  "RmveResource", // 0x9AD
  "RmveReference", // 0x9AE
  "ResError", // 0x9AF
  "WriteResource", // 0x9B0
  "CreateResFile", // 0x9B1
  "SystemEvent", // 0x9B2
  "SystemClick", // 0x9B3
  "SystemTask", // 0x9B4
  "SystemMenu", // 0x9B5
  "OpenDeskAcc", // 0x9B6
  "CloseDeskAcc", // 0x9B7
  "GetPattern", // 0x9B8
  "GetCursor", // 0x9B9
  "GetString", // 0x9BA
  "GetIcon", // 0x9BB
  "GetPicture", // 0x9BC
  "GetNewWindow", // 0x9BD
  "GetNewControl", // 0x9BE
  "GetRMenu", // 0x9BF
  "GetNewMBar", // 0x9C0
  "UniqueID", // 0x9C1
  "SysEdit", // 0x9C2
  "KeyTranslate/KeyTrans", // 0x9C3
  "OpenRFPerm", // 0x9C4
  "RsrcMapEntry", // 0x9C5
  "SecondsToDate/Secs2Date", // 0x9C6
  "DateToSeconds/Date2Secs", // 0x9C7
  "SysBeep", // 0x9C8
  "SysError", // 0x9C9
  "PutIcon", // 0x9CA
  "TEGetText", // 0x9CB
  "TEInit", // 0x9CC
  "TEDispose", // 0x9CD
  "TETextBox/TextBox", // 0x9CE
  "TESetText", // 0x9CF
  "TECalText", // 0x9D0
  "TESetSelect", // 0x9D1
  "TENew", // 0x9D2
  "TEUpdate", // 0x9D3
  "TEClick", // 0x9D4
  "TECopy", // 0x9D5
  "TECut", // 0x9D6
  "TEDelete", // 0x9D7
  "TEActivate", // 0x9D8
  "TEDeactivate", // 0x9D9
  "TEIdle", // 0x9DA
  "TEPaste", // 0x9DB
  "TEKey", // 0x9DC
  "TEScroll", // 0x9DD
  "TEInsert", // 0x9DE
  "TESetAlignment/TESetJust", // 0x9DF
  "Munger", // 0x9E0
  "HandToHand", // 0x9E1
  "PtrToXHand", // 0x9E2
  "PtrToHand", // 0x9E3
  "HandAndHand", // 0x9E4
  "InitPack", // 0x9E5
  "InitAllPacks", // 0x9E6
  {"Pack0/ListManager", {}, { // 0x9E7
    {0x0000, "LActivate"},
    {0x0004, "LAddColumn"},
    {0x0008, "LAddRow"},
    {0x000C, "LAddToCell"},
    {0x0010, "LAutoScroll"},
    {0x0014, "LCellSize"},
    {0x0018, "LClick"},
    {0x001C, "LClrCell"},
    {0x0020, "LDelColumn"},
    {0x0024, "LDelRow"},
    {0x0028, "LDispose"},
    {0x002C, "LDoDraw"},
    {0x0030, "LDraw"},
    {0x0034, "LFind"},
    {0x0038, "LGetCell"},
    {0x003C, "LGetSelect"},
    {0x0040, "LLastClick"},
    {0x0044, "LNew"},
    {0x0048, "LNextCell"},
    {0x004C, "LRect"},
    {0x0050, "LScroll"},
    {0x0054, "LSearch"},
    {0x0058, "LSetCell"},
    {0x005C, "LSetSelect"},
    {0x0060, "LSize"},
    {0x0064, "LUpdate"},
  }},
  "Pack1", // 0x9E8
  {"Pack2", {}, { // 0x9E9
    {0x0000, "DIBadMount"},
    {0x0002, "DILoad"},
    {0x0004, "DIUnload"},
    {0x0006, "DIFormat"},
    {0x0008, "DIVerify"},
    {0x000A, "DIZero"},
  }},
  {"Pack3", {}, { // 0x9EA
    {0x0001, "SFPutFile"},
    {0x0002, "SFGetFile"},
    {0x0003, "SFPPutFile"},
    {0x0004, "SFPGetFile"},
    {0x0005, "StandardPutFile"},
    {0x0006, "StandardGetFile"},
    {0x0007, "CustomPutFile"},
    {0x0008, "CustomGetFile"},
  }},
  {"Pack4/FP68K", {}, { // 0x9EB
    // Note: higher bits in the (16-bit) subroutine number is used for argument
    // types; these are just the subroutine nums with high bits cleared
    {0x0000, "FOADD"},
    {0x0001, "FOSETENV"},
    {0x0002, "FOSUB"},
    {0x0003, "FOGETENV"},
    {0x0004, "FOMUL"},
    {0x0005, "FOSETHV"},
    {0x0006, "FODIV"},
    {0x0007, "FOGETHV"},
    {0x0008, "FOCMP"},
    {0x0009, "FOD2B"},
    {0x000A, "FOCPX"},
    {0x000B, "FOB2D"},
    {0x000C, "FOREM"},
    {0x000D, "FONEG"},
    {0x000E, "FOZ2X"},
    {0x000F, "FOABS"},
    {0x0010, "FOX2Z"},
    {0x0011, "FOCPYSGN"},
    {0x0012, "FOSQRT"},
    {0x0013, "FONEXT"},
    {0x0014, "FORTI"},
    {0x0015, "FOSETXCP"},
    {0x0016, "FOTTI"},
    {0x0017, "FOPROCENTRY"},
    {0x0018, "FOSCALB"},
    {0x0019, "FOPROCEXIT"},
    {0x001A, "FOLOGB"},
    {0x001B, "FOTESTXCP"},
    {0x001C, "FOCLASS"},
  }, 0x00FF},
  {"Pack5/Elems68K", {}, { // 0x9EC
    // This pack has the same type info behavior (passed in subroutine number)
    // as Pack 4.
    {0x0000, "FOLNX"},
    {0x0002, "FOLOG2X"},
    {0x0004, "FOLN1X"},
    {0x0006, "FOLOG21X"},
    {0x0008, "FOEXPX"},
    {0x000A, "FOEXP2X"},
    {0x000C, "FOEXP1X"},
    {0x000E, "FOEXP21X"},
    {0x0010, "FOXPWRI"},
    {0x0012, "FOXPWRY"},
    {0x0014, "FOCOMPOUND"},
    {0x0016, "FOANNUITY"},
    {0x0018, "FOSINX"},
    {0x001A, "FOCOSX"},
    {0x001C, "FOTANX"},
    {0x001E, "FOATANX"},
    {0x0020, "FORANDX"},
  }, 0x00FF},
  {"Pack6", {}, { // 0x9ED
    {0x0000, "IUDateString"},
    {0x0002, "IUTimeString"},
    {0x0004, "IsMetric/IUMetric"},
    {0x0006, "GetIntlResource/IUGetIntl"},
    {0x0008, "IUSetIntl"},
    {0x000A, "IUMagString"},
    {0x000C, "IUMagIDString"},
    {0x000E, "DateString/IUDatePString"},
    {0x0010, "IUTimePString/TimeString"},
    {0x0014, "IULDateString/LongDateString"},
    {0x0016, "IULTimeString/LongTimeString"},
    {0x0018, "ClearIntlResourceCache/IUClearCache"},
    {0x001A, "CompareText/IUMagPString"},
    {0x001C, "IdenticalText/IUMagIDPString"},
    {0x001E, "IUScriptOrder/ScriptOrder"},
    {0x0020, "IULangOrder/LanguageOrder"},
    {0x0022, "IUTextOrder/TextOrder"},
    {0x0024, "GetIntlResourceTable/IUGetItlTable"},
  }},
  {"Pack7/DecStr68K", {}, { // 0x9EE
    {0x0000, "NumToString"},
    {0x0001, "StringToNum"},
    {0x0003, "Dec2Str"},
    {0x0002, "PStr2Dec"},
    {0x0004, "CStr2Dec"},
  }},
  "PtrAndHand", // 0x9EF
  "LoadSeg", // 0x9F0
  "UnloadSeg", // 0x9F1
  "Launch/LaunchApplication", // 0x9F2
  "Chain", // 0x9F3
  "ExitToShell", // 0x9F4
  "GetAppParms", // 0x9F5
  "GetResFileAttrs", // 0x9F6
  "SetResFileAttrs", // 0x9F7
  "MethodDispatch", // 0x9F8
  "InfoScrap", // 0x9F9
  "UnloadScrap/UnlodeScrap", // 0x9FA
  "LoadScrap/LodeScrap", // 0x9FB
  "ZeroScrap", // 0x9FC
  "GetScrap", // 0x9FD
  "PutScrap", // 0x9FE
  "Debugger", // 0x9FF
  "OpenCPort", // 0xA00
  "InitCPort", // 0xA01
  "CloseCPort", // 0xA02
  "NewPixMap", // 0xA03
  "DisposePixMap/DisposPixMap", // 0xA04
  "CopyPixMap", // 0xA05
  "SetPortPix/SetCPortPix", // 0xA06
  "NewPixPat", // 0xA07
  "DisposePixPat/DisposPixPat", // 0xA08
  "CopyPixPat", // 0xA09
  "PenPixPat", // 0xA0A
  "BackPixPat", // 0xA0B
  "GetPixPat", // 0xA0C
  "MakeRGBPat", // 0xA0D
  "FillCRect", // 0xA0E
  "FillCOval", // 0xA0F
  "FillCRoundRect", // 0xA10
  "FillCArc", // 0xA11
  "FillCRgn", // 0xA12
  "FillCPoly", // 0xA13
  "RGBForeColor", // 0xA14
  "RGBBackColor", // 0xA15
  "SetCPixel", // 0xA16
  "GetCPixel", // 0xA17
  "GetCTable", // 0xA18
  "GetForeColor", // 0xA19
  "GetBackColor", // 0xA1A
  "GetCCursor", // 0xA1B
  "SetCCursor", // 0xA1C
  "AllocCursor", // 0xA1D
  "GetCIcon", // 0xA1E
  "PlotCIcon", // 0xA1F
  "OpenCPicture", // 0xA20
  "OpColor", // 0xA21
  "HiliteColor", // 0xA22
  "CharExtra", // 0xA23
  "DisposeCTable/DisposCTable", // 0xA24
  "DisposeCIcon/DisposCIcon", // 0xA25
  "DIsposeCCursor/DisposCCursor", // 0xA26
  "GetMaxDevice", // 0xA27
  "GetCTSeed", // 0xA28
  "GetDeviceList", // 0xA29
  "GetMainDevice", // 0xA2A
  "GetNextDevice", // 0xA2B
  "TestDeviceAttribute", // 0xA2C
  "SetDeviceAttribute", // 0xA2D
  "InitGDevice", // 0xA2E
  "NewGDevice", // 0xA2F
  "DisposeGDevice/DisposGDevice", // 0xA30
  "SetGDevice", // 0xA31
  "GetGDevice", // 0xA32
  "Color2Index", // 0xA33
  "Index2Color", // 0xA34
  "InvertColor", // 0xA35
  "RealColor", // 0xA36
  "GetSubTable", // 0xA37
  "UpdatePixMap", // 0xA38
  "MakeITable", // 0xA39
  "AddSearch", // 0xA3A
  "AddComp", // 0xA3B
  "SetClientID", // 0xA3C
  "ProtectEntry", // 0xA3D
  "ReserveEntry", // 0xA3E
  "SetEntries", // 0xA3F
  "QDError", // 0xA40
  "SetWinColor", // 0xA41
  "GetAuxWin", // 0xA42
  "SetControlColor/SetCtlColor", // 0xA43
  "GetAuxiliaryControlRecord/GetAuxCtl", // 0xA44
  "NewCWindow", // 0xA45
  "GetNewCWindow", // 0xA46
  "SetDeskCPat", // 0xA47
  "GetCWMgrPort", // 0xA48
  "SaveEntries", // 0xA49
  "RestoreEntries", // 0xA4A
  "NewColorDialog/NewCDialog", // 0xA4B
  "DelSearch", // 0xA4C
  "DelComp", // 0xA4D
  "SetStdCProcs", // 0xA4E
  "CalcCMask", // 0xA4F
  "SeedCFill", // 0xA50
  "CopyDeepMask", // 0xA51
  {"HFSPinaforeDispatch/HighLevelFSDispatch", {}, { // 0xA52
    {0x0001, "FSMakeFSSpec"},
    {0x0002, "FSpOpenDF"},
    {0x0003, "FSpOpenRF"},
    {0x0004, "FSpCreate"},
    {0x0005, "FSpDirCreate"},
    {0x0006, "FSpDelete"},
    {0x0007, "FSpGetFInfo"},
    {0x0008, "FSpSetFInfo"},
    {0x0009, "FSpSetFLock"},
    {0x000A, "FSpRstFLock"},
    {0x000B, "FSpRename"},
    {0x000C, "FSpCatMove"},
    {0x000D, "FSpOpenResFile"},
    {0x000E, "FSpCreateResFile"},
    {0x000F, "FSpExchangeFiles"},
  }},
  {"DictionaryDispatch", {}, { // 0xA53
    {0x0500, "InitializeDictionary"},
    {0x0501, "OpenDictionary"},
    {0x0202, "CloseDictionary"},
    {0x0703, "InsertRecordToDictionary"},
    {0x0404, "DeleteRecordFromDictionary"},
    {0x0805, "FindRecordInDictionary"},
    {0x0A06, "FindRecordByIndexInDictionary"},
    {0x0407, "GetDictionaryInformation"},
    {0x0208, "CompactDictionary"},
  }},
  {"TextServicesDispatch", {}, { // 0xA54
    {0x0000, "NewTSMDocument"},
    {0x0001, "DeleteTSMDocument"},
    {0x0002, "ActivateTSMDocument"},
    {0x0003, "DeactivateTSMDocument"},
    {0x0004, "TSMEvent"},
    {0x0005, "TSMMenuSelect"},
    {0x0006, "SetTSMCursor"},
    {0x0007, "FixTSMDocument"},
    {0x0008, "GetServiceList"},
    {0x0009, "OpenTextService"},
    {0x000A, "CloseTextService"},
    {0x000B, "SendAEFromTSMComponent"},
    {0x000C, "SetDefaultInputMethod"},
    {0x000D, "GetDefaultInputMethod"},
    {0x000E, "SetTextServiceLanguage"},
    {0x000F, "GetTextServiceLanguage"},
    {0x0010, "UseInputWindow"},
    {0x0011, "NewServiceWindow"},
    {0x0012, "CloseServiceWindow"},
    {0x0013, "GetFrontServiceWindow"},
    {0x0014, "InitTSMAwareApplication"},
    {0x0015, "CloseTSMAwareApplication"},
    {0x0017, "FindServiceWindow"},
  }},
  "KobeMgr", // 0xA55
  "SpeechRecognitionDispatch", // 0xA56
  "DockingDispatch", // 0xA57
  "NewKernelDispatch", // 0xA58
  "MixedModeDispatch", // 0xA59
  "CodeFragmentDispatch", // 0xA5A
  "PBRemoveAccess", // 0xA5B
  "OCEUtils", // 0xA5C
  "DigitalSignature", // 0xA5D
  "OCETBDispatch/TBDispatch", // 0xA5E
  "OCEAuthentication", // 0xA5F
  "DeleteMCEntries/DelMCEntries", // 0xA60
  "GetMCInfo", // 0xA61
  "SetMCInfo", // 0xA62
  "DisposeMCInfo/DispMCInfo/DispMCEntries", // 0xA63
  "GetMCEntry", // 0xA64
  "SetMCEntries", // 0xA65
  "MenuChoice", // 0xA66
  "ModalDialogMenuSetup", // 0xA67
  {"DialogDispatch", {}, { // 0xA68
    {0x0203, "GetStdFilterProc"},
    {0x0304, "SetDialogDefaultItem"},
    {0x0305, "SetDialogCancelItem"},
    {0x0306, "SetDialogTracksCursor"},
  }},
  "UserNameNotification", // 0xA69
  "DeviceMgr", // 0xA6A
  "PowerPCFuture", // 0xA6B
  "PenMacMgr", // 0xA6C
  "LanguageMgr", // 0xA6D
  "AppleGuideDispatch", // 0xA6E
  nullptr, // 0xA6F
  nullptr, // 0xA70
  nullptr, // 0xA71
  nullptr, // 0xA72
  "ControlDispatch", // 0xA73
  "AppearanceDispatch", // 0xA74
  nullptr, // 0xA75
  nullptr, // 0xA76
  nullptr, // 0xA77
  nullptr, // 0xA78
  nullptr, // 0xA79
  nullptr, // 0xA7A
  nullptr, // 0xA7B
  nullptr, // 0xA7C
  nullptr, // 0xA7D
  nullptr, // 0xA7E
  nullptr, // 0xA7F
  "AVLTreeDispatch", // 0xA80
  nullptr, // 0xA81
  nullptr, // 0xA82
  nullptr, // 0xA83
  nullptr, // 0xA84
  nullptr, // 0xA85
  nullptr, // 0xA86
  nullptr, // 0xA87
  nullptr, // 0xA88
  nullptr, // 0xA89
  nullptr, // 0xA8A
  nullptr, // 0xA8B
  nullptr, // 0xA8C
  nullptr, // 0xA8D
  nullptr, // 0xA8E
  nullptr, // 0xA8F
  "InitPalettes", // 0xA90
  "NewPalette", // 0xA91
  "GetNewPalette", // 0xA92
  "DisposePalette", // 0xA93
  "ActivatePalette", // 0xA94
  "SetPalette/NSetPalette", // 0xA95
  "GetPalette", // 0xA96
  "PmForeColor", // 0xA97
  "PmBackColor", // 0xA98
  "AnimateEntry", // 0xA99
  "AnimatePalette", // 0xA9A
  "GetEntryColor", // 0xA9B
  "SetEntryColor", // 0xA9C
  "GetEntryUsage", // 0xA9D
  "SetEntryUsage", // 0xA9E
  "CTab2Palette", // 0xA9F
  "Palette2CTab", // 0xAA0
  "CopyPalette", // 0xAA1
  {"PaletteDispatch", {}, { // 0xAA2
    {0x0000, "Entry2Index"},
    {0x0002, "RestoreDeviceClut"},
    {0x0003, "ResizePalette"},
    {0x0015, "PMgrVersion"},
    {0x040D, "SaveFore"},
    {0x040E, "SaveBack"},
    {0x040F, "RestoreFore"},
    {0x0410, "RestoreBack"},
    {0x0417, "GetPaletteUpdates"},
    {0x0616, "SetPaletteUpdates"},
    {0x0A13, "SetDepth"},
    {0x0A14, "HasDepth"},
    {0x0C19, "GetGray"},
  }},
  "CodecDispatch", // 0xAA3
  "ALMDispatch", // 0xAA4
  nullptr, // 0xAA5
  nullptr, // 0xAA6
  nullptr, // 0xAA7
  nullptr, // 0xAA8
  nullptr, // 0xAA9
  "QuickTimeDispatch", // 0xAAA
  nullptr, // 0xAAB
  nullptr, // 0xAAC
  nullptr, // 0xAAD
  nullptr, // 0xAAE
  nullptr, // 0xAAF
  nullptr, // 0xAB0
  nullptr, // 0xAB1
  nullptr, // 0xAB2
  nullptr, // 0xAB3
  nullptr, // 0xAB4
  nullptr, // 0xAB5
  nullptr, // 0xAB6
  nullptr, // 0xAB7
  nullptr, // 0xAB8
  nullptr, // 0xAB9
  nullptr, // 0xABA
  nullptr, // 0xABB
  nullptr, // 0xABC
  nullptr, // 0xABD
  nullptr, // 0xABE
  nullptr, // 0xABF
  nullptr, // 0xAC0
  nullptr, // 0xAC1
  nullptr, // 0xAC2
  nullptr, // 0xAC3
  nullptr, // 0xAC4
  nullptr, // 0xAC5
  nullptr, // 0xAC6
  nullptr, // 0xAC7
  nullptr, // 0xAC8
  nullptr, // 0xAC9
  nullptr, // 0xACA
  nullptr, // 0xACB
  nullptr, // 0xACC
  nullptr, // 0xACD
  nullptr, // 0xACE
  nullptr, // 0xACF
  nullptr, // 0xAD0
  nullptr, // 0xAD1
  nullptr, // 0xAD2
  nullptr, // 0xAD3
  nullptr, // 0xAD4
  nullptr, // 0xAD5
  nullptr, // 0xAD6
  nullptr, // 0xAD7
  nullptr, // 0xAD8
  nullptr, // 0xAD9
  nullptr, // 0xADA
  "CursorDeviceDispatch", // 0xADB
  nullptr, // 0xADC
  "HumanInterfaceUtilsDispatch", // 0xADD
  nullptr, // 0xADE
  nullptr, // 0xADF
  nullptr, // 0xAE0
  nullptr, // 0xAE1
  nullptr, // 0xAE2
  nullptr, // 0xAE3
  nullptr, // 0xAE4
  nullptr, // 0xAE5
  nullptr, // 0xAE6
  nullptr, // 0xAE7
  nullptr, // 0xAE8
  nullptr, // 0xAE9
  nullptr, // 0xAEA
  nullptr, // 0xAEB
  nullptr, // 0xAEC
  nullptr, // 0xAED
  "AppleScript", // 0xAEE
  nullptr, // 0xAEF
  "PCCardDispatch", // 0xAF0
  "ATAMgr", // 0xAF1
  "ControlStripDispatch", // 0xAF2
  "ExpansionBusDispatch", // 0xAF3
  "InterruptMgr", // 0xAF4
  nullptr, // 0xAF5
  nullptr, // 0xAF6
  nullptr, // 0xAF7
  nullptr, // 0xAF8
  nullptr, // 0xAF9
  "InitApplication", // 0xAFA
  "CleanupApplication", // 0xAFB
  nullptr, // 0xAFC
  nullptr, // 0xAFD
  "MixedModeMagic", // 0xAFE
  nullptr, // 0xAFF
  "BitBlt", // 0xB00
  "BitsToMap", // 0xB01
  "BitsToPix", // 0xB02
  "Jackson", // 0xB03
  "ColorMap", // 0xB04
  "CopyHandle", // 0xB05
  "CullPoints", // 0xB06
  "PutPicByte", // 0xB07
  "PutPicOp", // 0xB08
  "DrawArc", // 0xB09
  "DrawLine", // 0xB0A
  "DrawSlab", // 0xB0B
  "FastSlabMode", // 0xB0C
  "GetSeek", // 0xB0D
  "MakeScaleTbl", // 0xB0E
  "CheckPic", // 0xB0F
  "DoLine", // 0xB10
  "OldPatToNew", // 0xB11
  "PackRgn", // 0xB12
  "PatConvert", // 0xB13
  "PatDither", // 0xB14
  "PatExpand", // 0xB15
  "PInit", // 0xB16
  "PortToMap", // 0xB17
  "PushVerb", // 0xB18
  "PutLine", // 0xB19
  "PutOval", // 0xB1A
  "PutRgn", // 0xB1B
  "NewTempBuffer", // 0xB1C
  {"QDExtensions", {}, { // 0xB1D
    {0x00000014, "OffscreenVersion"},
    {0x00040001, "LockPixels"},
    {0x00040002, "UnlockPixels"},
    {0x00040004, "DisposeGWorld"},
    {0x00040007, "CTabChanged"},
    {0x00040008, "PixPatChanged"},
    {0x00040009, "PortChanged"},
    {0x0004000A, "GDeviceChanged"},
    {0x0004000B, "AllowPurgePixels"},
    {0x0004000C, "NoPurgePixels"},
    {0x0004000D, "GetPixelsState"},
    {0x0004000F, "GetPixBaseAddr"},
    {0x00040011, "DisposeScreenBuffer"},
    {0x00040012, "GetGWorldDevice"},
    {0x00040013, "QDDone"},
    {0x00040016, "PixMap32Bit"},
    {0x00040017, "GetGWorldPixMap"},
    {0x00080005, "GetGWorld"},
    {0x00080006, "SetGWorld"},
    {0x0008000E, "SetPixelsState"},
    {0x000E0010, "NewScreenBuffer"},
    {0x000E0015, "NewTempScreenBuffer"},
    {0x00160000, "NewGWorld"},
    {0x00160003, "UpdateGWorld"},
  }},
  "DisposeTempBuffer", // 0xB1E
  "RgnBlit", // 0xB1F
  "RgnOp", // 0xB20
  "RSect", // 0xB21
  "SeekRgn", // 0xB22
  "SetFillPat", // 0xB23
  "SetUpStretch", // 0xB24
  "SlabMode", // 0xB25
  "SortPoints", // 0xB26
  "StretchBits", // 0xB27
  "StdDevLoop", // 0xB28
  "TrimRect", // 0xB29
  "XorSlab", // 0xB2A
  "ExTblPtr", // 0xB2B
  nullptr, // 0xB2C
  "NewTempHandle", // 0xB2D
  "PatExTbl", // 0xB2E
  nullptr, // 0xB2F
  "bMAIN0", // 0xB30
  "bMAIN1", // 0xB31
  "bMAIN2", // 0xB32
  "bMAIN3", // 0xB33
  "bSETUP8", // 0xB34
  "bMAIN9", // 0xB35
  "bSETUP10", // 0xB36
  "bMAIN11", // 0xB37
  "bXMAIN8", // 0xB38
  "bXMAIN9", // 0xB39
  "bXMAIN10", // 0xB3A
  "bXMAIN11", // 0xB3B
  "bcMain0", // 0xB3C
  "bcMain1", // 0xB3D
  "bHilite", // 0xB3E
  "bcMain3", // 0xB3F
  "bEND0", // 0xB40
  "bEND1", // 0xB41
  "bEND2", // 0xB42
  "bEND3", // 0xB43
  "bLONG8", // 0xB44
  "bEND9", // 0xB45
  "bEND10", // 0xB46
  "bEND11", // 0xB47
  "bXLONG8", // 0xB48
  "bXEND9", // 0xB49
  "bXEND10", // 0xB4A
  "bXEND11", // 0xB4B
  "bcEnd0", // 0xB4C
  "bcEnd1", // 0xB4D
  "bSlowHilite", // 0xB4E
  "bcEnd", // 0xB4F
  "bAvg", // 0xB50
  "bAddPin", // 0xB51
  "bAddOver", // 0xB52
  "bSubPin", // 0xB53
  "bTransparent", // 0xB54
  "bMax", // 0xB55
  "bSubOver", // 0xB56
  "bMin", // 0xB57
  "bSetup0", // 0xB58
  "bLeft0", // 0xB59
  "rMASK0", // 0xB5A
  "rMASK1", // 0xB5B
  "rMASK2", // 0xB5C
  "rMASK3", // 0xB5D
  "rMASK8", // 0xB5E
  "rMASK9", // 0xB5F
  "rMASK10", // 0xB60
  "rMASK11", // 0xB61
  "rXMASK8", // 0xB62
  "rXMASK9", // 0xB63
  "rXMASK10", // 0xB64
  "rXMASK11", // 0xB65
  "rAvg", // 0xB66
  "rAddPin", // 0xB67
  "rAddOver", // 0xB68
  "rSubPin", // 0xB69
  "rTransparent", // 0xB6A
  "rMax", // 0xB6B
  "rSubOver", // 0xB6C
  "rMin", // 0xB6D
  "rcMask0", // 0xB6E
  "rcMask1", // 0xB6F
  "rSlowHilite", // 0xB70
  "rcMask3", // 0xB71
  "rHilite", // 0xB72
  "stMASK0", // 0xB73
  "stMASK1", // 0xB74
  "stMASK2", // 0xB75
  "stMASK3", // 0xB76
  "stAvg", // 0xB77
  "stAddPin", // 0xB78
  "stAddOver", // 0xB79
  "stSubPin", // 0xB7A
  "stTransparent", // 0xB7B
  "stMax", // 0xB7C
  "stSubOver", // 0xB7D
  "stMin", // 0xB7E
  "stHilite", // 0xB7F
  "slMASK8", // 0xB80
  "slMASK9", // 0xB81
  "slMASK10", // 0xB82
  "slMASK11", // 0xB83
  "slXMASK8", // 0xB84
  "slXMASK9", // 0xB85
  "slXMASK10", // 0xB86
  "slXMASK11", // 0xB87
  "slAvg", // 0xB88
  "slAddPin", // 0xB89
  "slAddOver", // 0xB8A
  "slSubPin", // 0xB8B
  "slTransparent", // 0xB8C
  "slMax", // 0xB8D
  "slSubOver", // 0xB8E
  "slMin", // 0xB8F
  "slHilite", // 0xB90
  "ITabMatch", // 0xB91
  "ColorThing", // 0xB92
  "Pollack", // 0xB93
  "AllocRunBuf", // 0xB94
  "InitRgn", // 0xB95
  "ScaleBlt", // 0xB96
  "stNoStack", // 0xB97
  "BlitCase", // 0xB98
  "stScanLoop", // 0xB99
  "PicItem1", // 0xB9A
  "MakeGrayITab", // 0xB9B
  "FastLine", // 0xB9C
  "FastSlant", // 0xB9D
  "BitsDevLoop", // 0xB9E
  nullptr, // 0xB9F
  "rArith16Tab", // 0xBA0
  "rArith32Tab", // 0xBA1
  "rHiliteTab", // 0xBA2
  "gsRunTbl", // 0xBA3
  "gsExpTbl", // 0xBA4
  "gsSeekTbl", // 0xBA5
  "stArith16Tab", // 0xBA6
  "stArith32Tab", // 0xBA7
  "stColorTab", // 0xBA8
  "stGrayTab", // 0xBA9
  "stSearchTab", // 0xBAA
  "ScaleIndToInd", // 0xBAB
  "scIndTab1", // 0xBAC
  "scIndTab2", // 0xBAD
  "scIndTab4", // 0xBAE
  "scIndTab8", // 0xBAF
  "scIndTab16", // 0xBB0
  "scIndTab32", // 0xBB1
  "scDirTab1", // 0xBB2
  "scDirTab2", // 0xBB3
  "scDirTab4", // 0xBB4
  "scDirTab8", // 0xBB5
  "scDirTab16", // 0xBB6
  "scDirTab32", // 0xBB7
  "bArith16Tab", // 0xBB8
  "bArith32Tab", // 0xBB9
  "bHiliteTab", // 0xBBA
  "bArith16Setup", // 0xBBB
  "bArith32Setup", // 0xBBC
  "slArith16Tab", // 0xBBD
  "slArith32Tab", // 0xBBE
  "32QD", // 0xBBF
  "QDAlphaDispatch", // 0xBC0
  "QDStreamToMask", // 0xBC1
  "QTMatrixMathDispatch", // 0xBC2
  "NQDMisc", // 0xBC3
  "GetPMData", // 0xBC4
  "32QD", // 0xBC5
  "32QD", // 0xBC6
  "32QD", // 0xBC7
  "StdOpcodeProc", // 0xBC8 - BF8 is also StdOpcodeProc; is this entry wrong?
  {"IconDispatch", {}, { // 0xBC9
    {0x0207, "NewIconSuite"},
    {0x0217, "GetSuiteLabel"},
    {0x0302, "DisposeIconSuite"},
    {0x0316, "SetSuiteLabel"},
    {0x0419, "GetIconCacheData"},
    {0x041A, "SetIconCacheData"},
    {0x041B, "GetIconCacheProc"},
    {0x041C, "SetIconCacheProc"},
    {0x0500, "PlotIconID"},
    {0x0501, "GetIconSuite"},
    {0x050B, "GetLabel"},
    {0x0603, "PlotIconSuite"},
    {0x0604, "MakeIconCache"},
    {0x0606, "LoadIconCache"},
    {0x0608, "AddIconToSuite"},
    {0x0609, "GetIconFromSuite"},
    {0x060D, "PtInIconID"},
    {0x0610, "RectInIconID"},
    {0x0613, "IconIDToRgn"},
    {0x061D, "PlotIconHandle"},
    {0x061E, "PlotSICNHandle"},
    {0x061F, "PlotCIconHandle"},
    {0x070E, "PtInIconSuite"},
    {0x0711, "RectInIconSuite"},
    {0x0714, "IconSuiteToRgn"},
    {0x0805, "PlotIconMethod"},
    {0x080A, "ForEachIconDo"},
    {0x090F, "PtInIconMethod"},
    {0x0912, "RectInIconMethod"},
    {0x0915, "IconMethodToRgn"},
  }},
  "DeviceLoop", // 0xBCA
  nullptr, // 0xBCB
  "PBBlockMove", // 0xBCC
  "SnappingTurk", // 0xBCD
  "UnicodeMgr", // 0xBCE
  "ProcessMgr", // 0xBCF
  nullptr, // 0xBD0
  nullptr, // 0xBD1
  nullptr, // 0xBD2
  nullptr, // 0xBD3
  nullptr, // 0xBD4
  nullptr, // 0xBD5
  nullptr, // 0xBD6
  nullptr, // 0xBD7
  nullptr, // 0xBD8
  nullptr, // 0xBD9
  nullptr, // 0xBDA
  nullptr, // 0xBDB
  nullptr, // 0xBDC
  nullptr, // 0xBDD
  nullptr, // 0xBDE
  nullptr, // 0xBDF
  nullptr, // 0xBE0
  nullptr, // 0xBE1
  nullptr, // 0xBE2
  nullptr, // 0xBE3
  nullptr, // 0xBE4
  nullptr, // 0xBE5
  nullptr, // 0xBE6
  nullptr, // 0xBE7
  nullptr, // 0xBE8
  nullptr, // 0xBE9
  "ModemMgr", // 0xBEA
  "DisplayDispatch", // 0xBEB
  "ButtonMgr", // 0xBEC
  "DragDispatch", // 0xBED
  "ColorSync", // 0xBEE
  "TTSMgr", // 0xBEF
  "AROSE", // 0xBF0
  "GestaltValueDispatch", // 0xBF1
  "ThreadDispatch", // 0xBF2
  "EddyTrap", // 0xBF3
  "XTNDMgr", // 0xBF4
  "DSPManager", // 0xBF5
  "CollectionMgr", // 0xBF6
  "SynchIdleTime", // 0xBF7
  "StdOpcodeProc", // 0xBF8
  "AUXDispatch", // 0xBF9
  "AUXSysCall", // 0xBFA
  "MessageMgr", // 0xBFB
  "TranslationDispatch", // 0xBFC
  "TouchStone", // 0xBFD
  "GXPrinting", // 0xBFE
  "DebugStr", // 0xBFF
});

const TrapInfo* info_for_68k_trap(uint16_t trap_num, uint8_t flags) {
  try {
    const TrapInfo& t = (trap_num >= 0x800)
        ? toolbox_trap_info.at(trap_num - 0x800)
        : os_trap_info.at(trap_num);
    if (!t.name) {
      return nullptr;
    }
    try {
      return t.flag_overrides.at(flags).get();
    } catch (const out_of_range&) {
      return &t;
    }
  } catch (const out_of_range&) {
    return nullptr;
  }
}



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
