#include "TrapInfo.hh"

#include <map>
#include <unordered_map>
#include <vector>
#include <stdexcept>

using namespace std;



const vector<const char*> os_trap_names({
  "_Open", // 0x00
  "_Close", // 0x01
  "_Read", // 0x02
  "_Write", // 0x03
  "_Control", // 0x04
  "_Status", // 0x05
  "_KillIO", // 0x06
  "_GetVolInfo", // 0x07
  "_Create", // 0x08
  "_Delete", // 0x09
  "_OpenRF", // 0x0A
  "_Rename", // 0x0B
  "_GetFileInfo", // 0x0C
  "_SetFileInfo", // 0x0D
  "_UnmountVol", // 0x0E
  "_MountVol", // 0x0F
  "_Allocate", // 0x10
  "_GetEOF", // 0x11
  "_SetEOF", // 0x12
  "_FlushVol", // 0x13
  "_GetVol", // 0x14
  "_SetVol", // 0x15
  "_InitQueue", // 0x16
  "_Eject", // 0x17
  "_GetFPos", // 0x18
  "_InitZone", // 0x19
  "_GetZone", // 0x1A (called with flags as 0x11A)
  "_SetZone", // 0x1B
  "_FreeMem", // 0x1C
  "_MaxMem", // 0x1D (called with flags as 0x11D)
  "_NewPtr", // 0x1E
  "_DisposPtr", // 0x1F
  "_SetPtrSize", // 0x20
  "_GetPtrSize", // 0x21
  "_NewHandle", // 0x22 (called with flags as 0x122)
  "_DisposHandle", // 0x23
  "_SetHandleSize", // 0x24
  "_GetHandleSize", // 0x25
  "_HandleZone", // 0x26 (called with flags as 0x126)
  "_ReallocHandle", // 0x27
  "_RecoverHandle", // 0x28 (called with flags as 0x128)
  "_HLock", // 0x29
  "_HUnlock", // 0x2A
  "_EmptyHandle", // 0x2B
  "_InitApplZone", // 0x2C
  "_SetApplLimit", // 0x2D
  "_BlockMove", // 0x2E
  "_PostEvent", // 0x2F (called with flags as 0x12F)
  "_OSEventAvail", // 0x30
  "_GetOSEvent", // 0x31
  "_FlushEvents", // 0x32
  "_VInstall", // 0x33
  "_VRemove", // 0x34
  "_Offline", // 0x35
  "_MoreMasters", // 0x36
  nullptr, // 0x37
  "_WriteParam", // 0x38
  "_ReadDateTime", // 0x39
  "_SetDateTime", // 0x3A
  "_Delay", // 0x3B
  "_CmpString", // 0x3C
  "_DrvrInstall", // 0x3D
  "_DrvrRemove", // 0x3E
  "_InitUtil", // 0x3F
  "_ResrvMem", // 0x40
  "_SetFilLock", // 0x41
  "_RstFilLock", // 0x42
  "_SetFilType", // 0x43
  "_SetFPos", // 0x44
  "_FlushFile", // 0x45
  "_GetTrapAddress", // 0x46 (called with flags as 0x146)
  "_SetTrapAddress", // 0x47
  "_PtrZone", // 0x48 (called with flags as 0x148)
  "_HPurge", // 0x49
  "_HNoPurge", // 0x4A
  "_SetGrowZone", // 0x4B
  "_CompactMem", // 0x4C
  "_PurgeMem", // 0x4D
  "_AddDrive", // 0x4E
  "_RDrvrInstall", // 0x4F
  "_RelString", // 0x50
  nullptr, // 0x51
  nullptr, // 0x52
  nullptr, // 0x53
  "_UprString", // 0x54
  "_StripAddress", // 0x55
  nullptr, // 0x56
  "_SetAppBase", // 0x57
  nullptr, // 0x58
  nullptr, // 0x59
  nullptr, // 0x5A
  nullptr, // 0x5B
  nullptr, // 0x5C
  "_SwapMMUMode", // 0x5D
  nullptr, // 0x5E
  nullptr, // 0x5F
  "_HFSDispatch", // 0x60 (called with flags, so used as 0x260)
  "_MaxBlock", // 0x61
  "_PurgeSpace", // 0x62
  "_MaxApplZone", // 0x63
  "_MoveHHi", // 0x64
  "_StackSpace", // 0x65
  "_NewEmptyHandle", // 0x66
  "_HSetRBit", // 0x67
  "_HClrRBit", // 0x68
  "_HGetState", // 0x69
  "_HSetState", // 0x6A
  nullptr, // 0x6B
  nullptr, // 0x6C
  nullptr, // 0x6D
  "_SlotManager", // 0x6E
  "_SlotVInstall", // 0x6F
  "_SlotVRemove", // 0x70
  "_AttachVBL", // 0x71
  "_DoVBLTask", // 0x72
  nullptr, // 0x73
  nullptr, // 0x74
  "_SIntInstall", // 0x75
  "_SIntRemove", // 0x76
  "_CountADBs", // 0x77
  "_GetIndADB", // 0x78
  "_GetADBInfo", // 0x79
  "_SetADBInfo", // 0x7A
  "_ADBReInit", // 0x7B
  "_ADBOp", // 0x7C
  "_GetDefaultStartup", // 0x7D
  "_SetDefaultStartup", // 0x7E
  "_InternalWait", // 0x7F
  "_GetVideoDefault", // 0x80
  "_SetVideoDefault", // 0x81
  "_DTInstall", // 0x82
  "_SetOSDefault", // 0x83
  "_GetOSDefault", // 0x84
  nullptr, // 0x85
  nullptr, // 0x86
  nullptr, // 0x87
  nullptr, // 0x88
  nullptr, // 0x89
  nullptr, // 0x8A
  nullptr, // 0x8B
  nullptr, // 0x8C
  nullptr, // 0x8D
  nullptr, // 0x8E
  nullptr, // 0x8F
  "_SysEnvirons", // 0x90
});

const vector<const char*> toolbox_trap_names({
  nullptr, // 0x800
  nullptr, // 0x801
  nullptr, // 0x802
  nullptr, // 0x803
  nullptr, // 0x804
  nullptr, // 0x805
  nullptr, // 0x806
  nullptr, // 0x807
  "_InitProcMenu", // 0x808
  "_GetCVariant", // 0x809
  "_GetWVariant", // 0x80A
  "_PopUpMenuSelect", // 0x80B
  "_RGetResource", // 0x80C
  "_Count1Resources", // 0x80D
  "_Get1IxResource", // 0x80E
  "_Get1IxType", // 0x80F
  "_Unique1ID", // 0x810
  "_TESelView", // 0x811
  "_TEPinScroll", // 0x812
  "_TEAutoView", // 0x813
  nullptr, // 0x814
  "_SCSIDispatch", // 0x815
  "_Pack8", // 0x816
  "_CopyMask", // 0x817
  "_FixAtan2", // 0x818
  nullptr, // 0x819
  nullptr, // 0x81C
  nullptr, // 0x81B
  "_Count1Types", // 0x81C
  nullptr, // 0x81D
  nullptr, // 0x81E
  "_Get1Resource", // 0x81F
  "_Get1NamedResource", // 0x820
  "_MaxSizeRsrc", // 0x821
  nullptr, // 0x822
  nullptr, // 0x823
  nullptr, // 0x824
  nullptr, // 0x825
  "_InsMenuItem", // 0x826
  "_HideDItem", // 0x827
  "_ShowDItem", // 0x828
  nullptr, // 0x829
  nullptr, // 0x82A
  "_Pack9", // 0x82B
  "_Pack10", // 0x82C
  "_Pack11", // 0x82D
  "_Pack12", // 0x82E
  "_Pack13", // 0x82F
  "_Pack14", // 0x830
  "_Pack15", // 0x831
  nullptr, // 0x832
  nullptr, // 0x833
  "_SetFScaleDisable", // 0x834
  "_FontMetrics", // 0x835
  nullptr, // 0x836
  "_MeasureText", // 0x837
  "_CalcMask", // 0x838
  "_SeedFill", // 0x839
  "_ZoomWindow", // 0x83A
  "_TrackBox", // 0x83B
  "_TEGetOffset", // 0x83C
  "_TEDispatch", // 0x83D
  "_TEStyleNew", // 0x83E
  "_Long2Fix", // 0x83F
  "_Fix2Long", // 0x840
  "_Fix2Frac", // 0x841
  "_Frac2Fix", // 0x842
  "_Fix2X", // 0x843
  "_X2Fix", // 0x844
  "_Frac2X", // 0x845
  "_X2Frac", // 0x846
  "_FracCos", // 0x847
  "_FracSin", // 0x848
  "_FracSqrt", // 0x849
  "_FracMul", // 0x84A
  "_FracDiv", // 0x84B
  nullptr, // 0x84C
  "_FixDiv", // 0x84D
  "_GetItemCmd", // 0x84E
  "_SetItemCmd", // 0x84F
  "_InitCursor", // 0x850
  "_SetCursor", // 0x851
  "_HideCursor", // 0x852
  "_ShowCursor", // 0x853
  nullptr, // 0x854
  "_ShieldCursor", // 0x855
  "_ObscureCursor", // 0x856
  nullptr, // 0x857
  "_BitAnd", // 0x858
  "_BitXor", // 0x859
  "_BitNot", // 0x85A
  "_BitOr", // 0x85B
  "_BitShift", // 0x85C
  "_BitTst", // 0x85D
  "_BitSet", // 0x85E
  "_BitClr", // 0x85F
  nullptr, // 0x860
  "_Random", // 0x861
  "_ForeColor", // 0x862
  "_BackColor", // 0x863
  "_ColorBit", // 0x864
  "_GetPixel", // 0x865
  "_StuffHex", // 0x866
  "_LongMul", // 0x867
  "_FixMul", // 0x868
  "_FixRatio", // 0x869
  "_HiWord", // 0x86A
  "_LoWord", // 0x86B
  "_FixRound", // 0x86C
  "_InitPort", // 0x86D
  "_InitGraf", // 0x86E
  "_OpenPort", // 0x86F
  "_LocalToGlobal", // 0x870
  "_GlobalToLocal", // 0x871
  "_GrafDevice", // 0x872
  "_SetPort", // 0x873
  "_GetPort", // 0x874
  "_SetPBits", // 0x875
  "_PortSize", // 0x876
  "_MovePortTo", // 0x877
  "_SetOrigin", // 0x878
  "_SetClip", // 0x879
  "_GetClip", // 0x87A
  "_ClipRect", // 0x87B
  "_BackPat", // 0x87C
  "_ClosePort", // 0x87D
  "_AddPt", // 0x87E
  "_SubPt", // 0x87F
  "_SetPt", // 0x880
  "_EqualPt", // 0x881
  "_StdText", // 0x882
  "_DrawChar", // 0x883
  "_DrawString", // 0x884
  "_DrawText", // 0x885
  "_TextWidth", // 0x886
  "_TextFont", // 0x887
  "_TextFace", // 0x888
  "_TextMode", // 0x889
  "_TextSize", // 0x88A
  "_GetFontInfo", // 0x88B
  "_StringWidth", // 0x88C
  "_CharWidth", // 0x88D
  "_SpaceExtra", // 0x88E
  nullptr, // 0x88F
  "_StdLine", // 0x890
  "_LineTo", // 0x891
  "_Line", // 0x892
  "_MoveTo", // 0x893
  "_Move", // 0x894
  "_Shutdown", // 0x895
  "_HidePen", // 0x896
  "_ShowPen", // 0x897
  "_GetPenState", // 0x898
  "_SetPenState", // 0x899
  "_GetPen", // 0x89A
  "_PenSize", // 0x89B
  "_PenMode", // 0x89C
  "_PenPat", // 0x89D
  "_PenNormal", // 0x89E
  nullptr, // 0x89F
  "_StdRect", // 0x8A0
  "_FrameRect", // 0x8A1
  "_PaintRect", // 0x8A2
  "_EraseRect", // 0x8A3
  "_InverRect", // 0x8A4
  "_FillRect", // 0x8A5
  "_EqualRect", // 0x8A6
  "_SetRect", // 0x8A7
  "_OffsetRect", // 0x8A8
  "_InsetRect", // 0x8A9
  "_SectRect", // 0x8AA
  "_UnionRect", // 0x8AB
  "_Pt2Rect", // 0x8AC
  "_PtInRect", // 0x8AD
  "_EmptyRect", // 0x8AE
  "_StdRRect", // 0x8AF
  "_FrameRoundRect", // 0x8B0 // TODO: is this correct?
  "_PaintRoundRect", // 0x8B1
  "_EraseRoundRect", // 0x8B2
  "_InverRoundRect", // 0x8B3
  "_FillRoundRect", // 0x8B4
  "_ScriptUtil", // 0x8B5
  "_StdOval", // 0x8B6
  "_FrameOval", // 0x8B7
  "_PaintOval", // 0x8B8
  "_EraseOval", // 0x8B9
  "_InvertOval", // 0x8BA
  "_FillOval", // 0x8BB
  "_SlopeFromAngle", // 0x8BC
  "_StdArc", // 0x8BD
  "_FrameArc", // 0x8BE
  "_PaintArc", // 0x8BF
  "_EraseArc", // 0x8C0
  "_InvertArc", // 0x8C1
  "_FillArc", // 0x8C2
  "_PtToAngle", // 0x8C3
  "_AngleFromSlope", // 0x8C4
  "_StdPoly", // 0x8C5
  "_FramePoly", // 0x8C6
  "_PaintPoly", // 0x8C7
  "_ErasePoly", // 0x8C8
  "_InvertPoly", // 0x8C9
  "_FillPoly", // 0x8CA
  "_OpenPoly", // 0x8CB
  "_ClosePgon", // 0x8CC
  "_KillPoly", // 0x8CD
  "_OffsetPoly", // 0x8CE
  "_PackBits", // 0x8CF
  "_UnpackBits", // 0x8D0
  "_StdRgn", // 0x8D1
  "_FrameRgn", // 0x8D2
  "_PaintRgn", // 0x8D3
  "_EraseRgn", // 0x8D4
  "_InverRgn", // 0x8D5
  "_FillRgn", // 0x8D6
  nullptr, // 0x8D7
  "_NewRgn", // 0x8D8
  "_DisposRgn", // 0x8D9
  "_OpenRgn", // 0x8DA
  "_CloseRgn", // 0x8DB
  "_CopyRgn", // 0x8DC
  "_SetEmptyRgn", // 0x8DD
  "_SetRecRgn", // 0x8DE
  "_RectRgn", // 0x8DF
  "_OfsetRgn", // 0x8E0
  "_InsetRgn", // 0x8E1
  "_EmptyRgn", // 0x8E2
  "_EqualRgn", // 0x8E3
  "_SectRgn", // 0x8E4
  "_UnionRgn", // 0x8E5
  "_DiffRgn", // 0x8E6
  "_XorRgn", // 0x8E7
  "_PtInRgn", // 0x8E8
  "_RectInRgn", // 0x8E9
  "_SetStdProcs", // 0x8EA
  "_StdBits", // 0x8EB
  "_CopyBits", // 0x8EC
  "_StdTxMeas", // 0x8ED
  "_StdGetPic", // 0x8EE
  "_ScrollRect", // 0x8EF
  "_StdPutPic", // 0x8F0
  "_StdComment", // 0x8F1
  "_PicComment", // 0x8F2
  "_OpenPicture", // 0x8F3
  "_ClosePicture", // 0x8F4
  "_KillPicture", // 0x8F5
  "_DrawPicture", // 0x8F6
  nullptr, // 0x8F7
  "_ScalePt", // 0x8F8
  "_MapPt", // 0x8F9
  "_MapRect", // 0x8FA
  "_MapRgn", // 0x8FB
  "_MapPoly", // 0x8FC
  nullptr, // 0x8FD
  "_InitFonts", // 0x8FE
  "_GetFName", // 0x8FF
  "_GetFNum", // 0x900
  "_FMSwapFont", // 0x901
  "_RealFont", // 0x902
  "_SetFontLock", // 0x903
  "_DrawGrowIcon", // 0x904
  "_DragGrayRgn", // 0x905
  "_NewString", // 0x906
  "_SetString", // 0x907
  "_ShowHide", // 0x908
  "_CalcVis", // 0x909
  "_CalcVBehind", // 0x90A
  "_ClipAbove", // 0x90B
  "_PaintOne", // 0x90C
  "_PaintBehind", // 0x90D
  "_SaveOld", // 0x90E
  "_DrawNew", // 0x90F
  "_GetWMgrPort", // 0x910
  "_CheckUpdate", // 0x911
  "_InitWindows", // 0x912
  "_NewWindow", // 0x913
  "_DisposWindow", // 0x914
  "_ShowWindow", // 0x915
  "_HideWindow", // 0x916
  "_GetWRefCon", // 0x917
  "_SetWRefCon", // 0x918
  "_GetWTitle", // 0x919
  "_SetWTitle", // 0x91A
  "_MoveWindow", // 0x91B
  "_HiliteWindow", // 0x91C
  "_SizeWindow", // 0x91D
  "_TrackGoAway", // 0x91E
  "_SelectWindow", // 0x91F
  "_BringToFront", // 0x920
  "_SendBehind", // 0x921
  "_BeginUpdate", // 0x922
  "_EndUpdate", // 0x923
  "_FrontWindow", // 0x924
  "_DragWindow", // 0x925
  "_DragTheRgn", // 0x926
  "_InvalRgn", // 0x927
  "_InvalRect", // 0x928
  "_ValidRgn", // 0x929
  "_ValidRect", // 0x92A
  "_GrowWindow", // 0x92B
  "_FindWindow", // 0x92C
  "_CloseWindow", // 0x92D
  "_SetWindowPic", // 0x92E
  "_GetWindowPic", // 0x92F
  "_InitMenus", // 0x930
  "_NewMenu", // 0x931
  "_DisposMenu", // 0x932
  "_AppendMenu", // 0x933
  "_ClearMenuBar", // 0x934
  "_InsertMenu", // 0x935
  "_DeleteMenu", // 0x936
  "_DrawMenuBar", // 0x937
  "_HiliteMenu", // 0x938
  "_EnableItem", // 0x939
  "_DisableItem", // 0x93A
  "_GetMenuBar", // 0x93B
  "_SetMenuBar", // 0x93C
  "_MenuSelect", // 0x93D
  "_MenuKey", // 0x93E
  "_GetItmIcon", // 0x93F
  "_SetItmIcon", // 0x940
  "_GetItmStyle", // 0x941
  "_SetItmStyle", // 0x942
  "_GetItmMark", // 0x943
  "_SetItmMark", // 0x944
  "_CheckItem", // 0x945
  "_GetItem", // 0x946
  "_SetItem", // 0x947
  "_CalcMenuSize", // 0x948
  "_GetMHandle", // 0x949
  "_SetMFlash", // 0x94A
  "_PlotIcon", // 0x94B
  "_FlashMenuBar", // 0x94C
  "_AddResMenu", // 0x94D
  "_PinRect", // 0x94E
  "_DeltaPoint", // 0x94F
  "_CountMItems", // 0x950
  "_InsertResMenu", // 0x951
  "_DelMenuItem", // 0x952
  "_UpdtControl", // 0x953
  "_NewControl", // 0x954
  "_DisposControl", // 0x955
  "_KillControls", // 0x956
  "_ShowControl", // 0x957
  "_HideControl", // 0x958
  "_MoveControl", // 0x959
  "_GetCRefCon", // 0x95A
  "_SetCRefCon", // 0x95B
  "_SizeControl", // 0x95C
  "_HiliteControl", // 0x95D
  "_GetCTitle", // 0x95E
  "_SetCTitle", // 0x95F
  "_GetCtlValue", // 0x960
  "_GetMinCtl", // 0x961
  "_GetMaxCtl", // 0x962
  "_SetCtlValue", // 0x963
  "_SetMinCtl", // 0x964
  "_SetMaxCtl", // 0x965
  "_TestControl", // 0x966
  "_DragControl", // 0x967
  "_TrackControl", // 0x968
  "_DrawControls", // 0x969
  "_GetCtlAction", // 0x96A
  "_SetCtlAction", // 0x96B
  "_FindControl", // 0x96C
  "_Draw1Control", // 0x96D
  "_Dequeue", // 0x96E
  "_Enqueue", // 0x96F
  "_GetNextEvent", // 0x970
  "_EventAvail", // 0x971
  "_GetMouse", // 0x972
  "_StillDown", // 0x973
  "_Button", // 0x974
  "_TickCount", // 0x975
  "_GetKeys", // 0x976
  "_WaitMouseUp", // 0x977
  "_UpdtDialog", // 0x978
  "_CouldDialog", // 0x979
  "_FreeDialog", // 0x97A
  "_InitDialogs", // 0x97B
  "_GetNewDialog", // 0x97C
  "_NewDialog", // 0x97D
  "_SelIText", // 0x97E
  "_IsDialogEvent", // 0x97F
  "_DialogSelect", // 0x980
  "_DrawDialog", // 0x981
  "_CloseDialog", // 0x982
  "_DisposDialog", // 0x983
  "_FindDItem", // 0x984
  "_Alert", // 0x985
  "_StopAlert", // 0x986
  "_NoteAlert", // 0x987
  "_CautionAlert", // 0x988
  "_CouldAlert", // 0x989
  "_FreeAlert", // 0x98A
  "_ParamText", // 0x98B
  "_ErrorSound", // 0x98C
  "_GetDItem", // 0x98D
  "_SetDItem", // 0x98E
  "_SetIText", // 0x98F
  "_GetIText", // 0x990
  "_ModalDialog", // 0x991
  "_DetachResource", // 0x992
  "_SetResPurge", // 0x993
  "_CurResFile", // 0x994
  "_InitResources", // 0x995
  "_RsrcZoneInit", // 0x996
  "_OpenResFile", // 0x997
  "_UseResFile", // 0x998
  "_UpdateResFile", // 0x999
  "_CloseResFile", // 0x99A
  "_SetResLoad", // 0x99B
  "_CountResources", // 0x99C
  "_GetIndResource", // 0x99D
  "_CountTypes", // 0x99E
  "_GetIndType", // 0x99F
  "_GetResource", // 0x9A0
  "_GetNamedResource", // 0x9A1
  "_LoadResource", // 0x9A2
  "_ReleaseResource", // 0x9A3
  "_HomeResFile", // 0x9A4
  "_SizeRsrc", // 0x9A5
  "_GetResAttrs", // 0x9A6
  "_SetResAttrs", // 0x9A7
  "_GetResInfo", // 0x9A8
  "_SetResInfo", // 0x9A9
  "_ChangedResource", // 0x9AA
  "_AddResource", // 0x9AB
  "_AddReference", // 0x9AC
  "_RmveResource", // 0x9AD
  "_RmveReference", // 0x9AE
  "_ResError", // 0x9AF
  "_WriteResource", // 0x9B0
  "_CreateResFile", // 0x9B1
  "_SystemEvent", // 0x9B2
  "_SystemClick", // 0x9B3
  "_SystemTask", // 0x9B4
  "_SystemMenu", // 0x9B5
  "_OpenDeskAcc", // 0x9B6
  "_CloseDeskAcc", // 0x9B7
  "_GetPattern", // 0x9B8
  "_GetCursor", // 0x9B9
  "_GetString", // 0x9BA
  "GetIcon", // 0x9BB
  "_GetPicture", // 0x9BC
  "_GetNewWindow", // 0x9BD
  "_GetNewControl", // 0x9BE
  "_GetRMenu", // 0x9BF
  "_GetNewMBar", // 0x9C0
  "_UniqueID", // 0x9C1
  "_SysEdit", // 0x9C2
  "_KeyTrans", // 0x9C3
  "_OpenRFPerm", // 0x9C4
  "_RsrcMapEntry", // 0x9C5
  "_Secs2Date", // 0x9C6
  "_Date2Sec", // 0x9C7
  "_SysBeep", // 0x9C8
  "_SysError", // 0x9C9
  nullptr, ""// 0x9CA
  "_TEGetText", // 0x9CB
  "_TEInit", // 0x9CC
  "_TEDispose", // 0x9CD
  "_TextBox", // 0x9CE
  "_TESetText", // 0x9CF
  "_TECalText", // 0x9D0
  "_TESetSelect", // 0x9D1
  "_TENew", // 0x9D2
  "_TEUpdate", // 0x9D3
  "_TEClick", // 0x9D4
  "_TECopy", // 0x9D5
  "_TECut", // 0x9D6
  "_TEDelete", // 0x9D7
  "_TEActivate", // 0x9D8
  "_TEDeactivate", // 0x9D9
  "_TEIdle", // 0x9DA
  "_TEPaste", // 0x9DB
  "_TEKey", // 0x9DC
  "_TEScroll", // 0x9DD
  "_TEInsert", // 0x9DE
  "_TESetJust", // 0x9DF
  "_Munger", // 0x9E0
  "_HandToHand", // 0x9E1
  "_PtrToXHand", // 0x9E2
  "_PtrToHand", // 0x9E3
  "_HandAndHand", // 0x9E4
  "_InitPack", // 0x9E5
  "_InitAllPacks", // 0x9E6
  "_Pack0", // 0x9E7
  "_Pack1", // 0x9E8
  "_Pack2", // 0x9E9
  "_Pack3", // 0x9EA
  "_Pack4/_FP68K", // 0x9EB
  "_Pack5/_Elems68K", // 0x9EC
  "_Pack6", // 0x9ED
  "_Pack7/_DecStr68K", // 0x9EE
  "_PtrAndHand", // 0x9EF
  "_LoadSeg", // 0x9F0
  "_UnloadSeg", // 0x9F1
  "_Launch", // 0x9F2
  "_Chain", // 0x9F3
  "_ExitToShell", // 0x9F4
  "_GetAppParms", // 0x9F5
  "_GetResFileAttrs", // 0x9F6
  "_SetResFileAttrs", // 0x9F7
  nullptr, // 0x9F8
  "_InfoScrap", // 0x9F9
  "_UnlodeScrap", // 0x9FA
  "_LodeScrap", // 0x9FB
  "_ZeroScrap", // 0x9FC
  "_GetScrap", // 0x9FD
  "_PutScrap", // 0x9FE
  nullptr, // 0x9FF
  "_OpenCport", // 0xA00
  "_InitCport", // 0xA01
  nullptr, // 0xA02
  "_NewPixMap", // 0xA03
  "_DisposPixMap", // 0xA04
  "_CopyPixMap", // 0xA05
  "_SetCPortPix", // 0xA06
  "_NewPixPat", // 0xA07
  "_DisposPixPat", // 0xA08
  "_CopyPixPat", // 0xA09
  "_PenPixPat", // 0xA0A
  "_BackPixPat", // 0xA0B
  "_GetPixPat", // 0xA0C
  "_MakeRGBPat", // 0xA0D
  "_FillCRect", // 0xA0E
  "_FillCOval", // 0xA0F
  "_FillCRoundRect", // 0xA10
  "_FillCArc", // 0xA11
  "_FillCRgn", // 0xA12
  "_FillCPoly", // 0xA13
  "_RGBForeColor", // 0xA14
  "_RGBBackColor", // 0xA15
  "_SetCPixel", // 0xA16
  "_GetCPixel", // 0xA17
  "_GetCTable", // 0xA18
  "_GetForeColor", // 0xA19
  "_GetBackColor", // 0xA1A
  "_GetCCursor", // 0xA1B
  "_SetCCursor", // 0xA1C
  "_AllocCursor", // 0xA1D
  "_GetCIcon", // 0xA1E
  "_PlotCIcon", // 0xA1F
  nullptr, // 0xA20
  "_OpColor", // 0xA21
  "_HiliteColor", // 0xA22
  "_CharExtra", // 0xA23
  "_DisposCTable", // 0xA24
  "_DisposCIcon", // 0xA25
  "_DisposCCursor", // 0xA26
  "_GetMaxDevice", // 0xA27
  nullptr, // 0xA28
  "_GetDeviceList", // 0xA29
  "_GetMainDevice", // 0xA2A
  "_GetNextDevice", // 0xA2B
  "_TestDeviceAttribute", // 0xA2C
  "_SetDeviceAttribute", // 0xA2D
  "_InitGDevice", // 0xA2E
  "_NewGDevice", // 0xA2F
  "_DisposGDevice", // 0xA30
  "_SetGDevice", // 0xA31
  "_GetGDevice", // 0xA32
  "_Color2Index", // 0xA33
  "_Index2Color", // 0xA34
  "_InvertColor", // 0xA35
  "_RealColor", // 0xA36
  "_GetSubTable", // 0xA37
  nullptr, // 0xA38
  "_MakeITable", // 0xA39
  "_AddSearch", // 0xA3A
  "_AddComp", // 0xA3B
  "_SetClientID", // 0xA3C
  "_ProtectEntry", // 0xA3D
  "_ReserveEntry", // 0xA3E
  "_SetEntries", // 0xA3F
  "_QDError", // 0xA40
  "_SetWinColor", // 0xA41
  "_GetAuxWin", // 0xA42
  "_SetCtlColor", // 0xA43
  "_GetAuxCtl", // 0xA44
  "_NewCWindow", // 0xA45
  "_GetNewCWindow", // 0xA46
  "_SetDeskCPat", // 0xA47
  "_GetCWMgrPort", // 0xA48
  "_SaveEntries", // 0xA49
  "_RestoreEntries", // 0xA4A
  "_NewCDialog", // 0xA4B
  "_DelSearch", // 0xA4C
  "_DelComp", // 0xA4D
  nullptr, // 0xA4E
  "_CalcCMask", // 0xA4F
  "_SeedCFill", // 0xA50
  nullptr, // 0xA51
  nullptr, // 0xA52
  nullptr, // 0xA53
  nullptr, // 0xA54
  nullptr, // 0xA55
  nullptr, // 0xA56
  nullptr, // 0xA57
  nullptr, // 0xA58
  nullptr, // 0xA59
  nullptr, // 0xA5A
  nullptr, // 0xA5B
  nullptr, // 0xA5C
  nullptr, // 0xA5D
  nullptr, // 0xA5E
  nullptr, // 0xA5F
  "_DelMCEntries", // 0xA60
  "_GetMCInfo", // 0xA61
  "_SetMCInfo", // 0xA62
  "_DispMCEntries", // 0xA63
  "_GetMCEntry", // 0xA64
  "_SetMCEntries", // 0xA65
  "_MenuChoice", // 0xA66
});

const char* name_for_68k_trap(uint16_t trap_num) {
  try {
    if (trap_num >= 0x800) {
      return toolbox_trap_names.at(trap_num - 0x800);
    } else {
      return os_trap_names.at(trap_num);
    }
  } catch (const out_of_range&) {
    return nullptr;
  }
}



static constexpr uint32_t make_pack_trap_id(uint16_t trap_num, uint16_t sel) {
  return static_cast<uint32_t>(trap_num << 16) | sel;
}

unordered_map<uint32_t, const char*> pack_trap_names({
  // These are like {0xXXXXYYYY, "name"}; X is parent trap, Y is subroutine

  // Pack 0 (0x09E7) - list manager; subroutine number passed via stack (word)
  {make_pack_trap_id(0x09E7,   0), "LActivate"},
  {make_pack_trap_id(0x09E7,   4), "LAddColumn"},
  {make_pack_trap_id(0x09E7,   8), "LAddRow"},
  {make_pack_trap_id(0x09E7,  12), "LAddToCell"},
  {make_pack_trap_id(0x09E7,  16), "LAutoScroll"},
  {make_pack_trap_id(0x09E7,  20), "LCellSize"},
  {make_pack_trap_id(0x09E7,  24), "LClick"},
  {make_pack_trap_id(0x09E7,  28), "LClrCell"},
  {make_pack_trap_id(0x09E7,  32), "LDelColumn"},
  {make_pack_trap_id(0x09E7,  36), "LDelRow"},
  {make_pack_trap_id(0x09E7,  40), "LDispose"},
  {make_pack_trap_id(0x09E7,  44), "LDoDraw"},
  {make_pack_trap_id(0x09E7,  48), "LDraw"},
  {make_pack_trap_id(0x09E7,  52), "LFind"},
  {make_pack_trap_id(0x09E7,  56), "LGetCell"},
  {make_pack_trap_id(0x09E7,  60), "LGetSelect"},
  {make_pack_trap_id(0x09E7,  64), "LLastClick"},
  {make_pack_trap_id(0x09E7,  68), "LNew"},
  {make_pack_trap_id(0x09E7,  72), "LNextCell"},
  {make_pack_trap_id(0x09E7,  76), "LRect"},
  {make_pack_trap_id(0x09E7,  80), "LScroll"},
  {make_pack_trap_id(0x09E7,  84), "LSearch"},
  {make_pack_trap_id(0x09E7,  88), "LSetCell"},
  {make_pack_trap_id(0x09E7,  92), "LSetSelect"},
  {make_pack_trap_id(0x09E7,  96), "LSize"},
  {make_pack_trap_id(0x09E7, 100), "LUpdate"},

  // Pack 1 (0x09E8) - reserved

  // Pack 2 (0x09E9) - disk initialization; subroutine number passed via stack (word)
  {make_pack_trap_id(0x09E9,  0), "DIBadMount"},
  {make_pack_trap_id(0x09E9,  2), "DILoad"},
  {make_pack_trap_id(0x09E9,  4), "DIUnload"},
  {make_pack_trap_id(0x09E9,  6), "DIFormat"},
  {make_pack_trap_id(0x09E9,  8), "DIVerify"},
  {make_pack_trap_id(0x09E9, 10), "DIZero"},

  // Pack 3 (0x09EA) - standard file; subroutine number passed via stack (word)
  {make_pack_trap_id(0x09EA, 1), "SFPutFile"},
  {make_pack_trap_id(0x09EA, 2), "SFGetFile"},
  {make_pack_trap_id(0x09EA, 3), "SFPPutFile"},
  {make_pack_trap_id(0x09EA, 4), "SFPGetFile"},

  // Pack 4 (0x09EB) - floating-point math; subroutine number passed via stack (word)
  // Note: higher bits in the (16-bit) subroutine number is used for argument
  // types; these are just the subroutine nums with high bits cleared.
  {make_pack_trap_id(0x09EB,  0), "FOADD"},
  {make_pack_trap_id(0x09EB,  1), "FOSETENV"},
  {make_pack_trap_id(0x09EB,  2), "FOSUB"},
  {make_pack_trap_id(0x09EB,  3), "FOGETENV"},
  {make_pack_trap_id(0x09EB,  4), "FOMUL"},
  {make_pack_trap_id(0x09EB,  5), "FOSETHV"},
  {make_pack_trap_id(0x09EB,  6), "FODIV"},
  {make_pack_trap_id(0x09EB,  7), "FOGETHV"},
  {make_pack_trap_id(0x09EB,  8), "FOCMP"},
  {make_pack_trap_id(0x09EB,  9), "FOD2B"},
  {make_pack_trap_id(0x09EB, 10), "FOCPX"},
  {make_pack_trap_id(0x09EB, 11), "FOB2D"},
  {make_pack_trap_id(0x09EB, 12), "FOREM"},
  {make_pack_trap_id(0x09EB, 13), "FONEG"},
  {make_pack_trap_id(0x09EB, 14), "FOZ2X"},
  {make_pack_trap_id(0x09EB, 15), "FOABS"},
  {make_pack_trap_id(0x09EB, 16), "FOX2Z"},
  {make_pack_trap_id(0x09EB, 17), "FOCPYSGN"},
  {make_pack_trap_id(0x09EB, 18), "FOSQRT"},
  {make_pack_trap_id(0x09EB, 19), "FONEXT"},
  {make_pack_trap_id(0x09EB, 20), "FORTI"},
  {make_pack_trap_id(0x09EB, 21), "FOSETXCP"},
  {make_pack_trap_id(0x09EB, 22), "FOTTI"},
  {make_pack_trap_id(0x09EB, 23), "FOPROCENTRY"},
  {make_pack_trap_id(0x09EB, 24), "FOSCALB"},
  {make_pack_trap_id(0x09EB, 25), "FOPROCEXIT"},
  {make_pack_trap_id(0x09EB, 26), "FOLOGB"},
  {make_pack_trap_id(0x09EB, 27), "FOTESTXCP"},
  {make_pack_trap_id(0x09EB, 28), "FOCLASS"},

  // Pack 5 (0x09EC) - transcendental functions; subroutine number passed via stack (word)
  // This pack has the same type info behavior (passed in subroutine number) as
  // Pack 4.
  {make_pack_trap_id(0x09EC,     0), "FOLNX"},
  {make_pack_trap_id(0x09EC,     2), "FOLOG2X"},
  {make_pack_trap_id(0x09EC,     4), "FOLN1X"},
  {make_pack_trap_id(0x09EC,     6), "FOLOG21X"},
  {make_pack_trap_id(0x09EC,     8), "FOEXPX"},
  {make_pack_trap_id(0x09EC,    10), "FOEXP2X"},
  {make_pack_trap_id(0x09EC,    12), "FOEXP1X"},
  {make_pack_trap_id(0x09EC,    14), "FOEXP21X"},
  {make_pack_trap_id(0x09EC,    24), "FOSINX"},
  {make_pack_trap_id(0x09EC,    26), "FOCOSX"},
  {make_pack_trap_id(0x09EC,    28), "FOTANX"},
  {make_pack_trap_id(0x09EC,    30), "FOATANX"},
  {make_pack_trap_id(0x09EC,    32), "FORANDX"},
  {make_pack_trap_id(0x09EC, 32784), "FOXPWRI"},
  {make_pack_trap_id(0x09EC, 32786), "FOXPWRY"},
  {make_pack_trap_id(0x09EC, 49172), "FOCOMPOUND"},
  {make_pack_trap_id(0x09EC, 49174), "FOANNUITY"},

  // Pack 6 (0x09ED) - international utilities; subroutine number passed via stack (word)
  {make_pack_trap_id(0x09ED,  0), "IUDateString"},
  {make_pack_trap_id(0x09ED,  2), "IUTimeString"},
  {make_pack_trap_id(0x09ED,  4), "IUMetric"},
  {make_pack_trap_id(0x09ED,  6), "IUGetIntl"},
  {make_pack_trap_id(0x09ED,  8), "IUSetIntl"},
  {make_pack_trap_id(0x09ED, 10), "IUMagString"},
  {make_pack_trap_id(0x09ED, 12), "IUMagIDString"},
  {make_pack_trap_id(0x09ED, 14), "IUDatePString"},
  {make_pack_trap_id(0x09ED, 16), "IUTimePString"},

  // Pack 7 (0x09EE) - binary/decimal conversion; subroutine number passed via stack (word)
  {make_pack_trap_id(0x09EE, 4), "CStr2Dec"},
  {make_pack_trap_id(0x09EE, 0), "NumToString"},
  {make_pack_trap_id(0x09EE, 1), "StringToNum"},
  {make_pack_trap_id(0x09EE, 3), "Dec2Str"},
  {make_pack_trap_id(0x09EE, 2), "PStr2Dec"},

  // _HFSDispatch (0x60) - subroutine number passed in D0
  {make_pack_trap_id(0x0060, 0x0001), "PBOpenWD"},
  {make_pack_trap_id(0x0060, 0x0002), "PBCloseWD"},
  {make_pack_trap_id(0x0060, 0x0005), "PBCatMove"},
  {make_pack_trap_id(0x0060, 0x0006), "PBDirCreate"},
  {make_pack_trap_id(0x0060, 0x0007), "PBGetWDInfo"},
  {make_pack_trap_id(0x0060, 0x0008), "PBGetFCBInfo"},
  {make_pack_trap_id(0x0060, 0x0009), "PBGetCatInfo"},
  {make_pack_trap_id(0x0060, 0x000A), "PBSetCatInfo"},
  {make_pack_trap_id(0x0060, 0x000B), "PBSetVInfo"},
  {make_pack_trap_id(0x0060, 0x0010), "PBLockRange"},
  {make_pack_trap_id(0x0060, 0x0011), "PBUnlockRange"},
  {make_pack_trap_id(0x0060, 0x0014), "PBCreateFileIDRef"},
  {make_pack_trap_id(0x0060, 0x0015), "PBDeleteFileIDRef"},
  {make_pack_trap_id(0x0060, 0x0016), "PBResolveFileIDRef/LockRng"},
  {make_pack_trap_id(0x0060, 0x0017), "PBExchangeFiles/UnlockRng"},
  {make_pack_trap_id(0x0060, 0x0018), "PBCatSearch"},
  {make_pack_trap_id(0x0060, 0x001A), "PBHOpenDF"},
  {make_pack_trap_id(0x0060, 0x001B), "PBMakeFSSpec"},
  {make_pack_trap_id(0x0060, 0x0030), "PBHGetVolParms"},
  {make_pack_trap_id(0x0060, 0x0031), "PBHGetLogInInfo"},
  {make_pack_trap_id(0x0060, 0x0032), "PBHGetDirAccess"},
  {make_pack_trap_id(0x0060, 0x0033), "PBHSetDirAccess"},
  {make_pack_trap_id(0x0060, 0x0034), "PBHMapID"},
  {make_pack_trap_id(0x0060, 0x0035), "PBHMapName"},
  {make_pack_trap_id(0x0060, 0x0036), "PBHCopyFile"},
  {make_pack_trap_id(0x0060, 0x0037), "PBHMoveRename"},
  {make_pack_trap_id(0x0060, 0x0038), "PBHOpenDeny"},
  {make_pack_trap_id(0x0060, 0x0039), "PBHOpenRFDeny"},
  {make_pack_trap_id(0x0060, 0x003F), "PBGetVolMountInfoSize"},
  {make_pack_trap_id(0x0060, 0x0040), "PBGetVolMountInfo"},
  {make_pack_trap_id(0x0060, 0x0041), "PBVolumeMount"},
  {make_pack_trap_id(0x0060, 0x0042), "PBShare"},
  {make_pack_trap_id(0x0060, 0x0043), "PBUnshare"},
  {make_pack_trap_id(0x0060, 0x0044), "PBGetUGEntry"},
  {make_pack_trap_id(0x0060, 0x0060), "PBGetForeignPrivs"},
  {make_pack_trap_id(0x0060, 0x0061), "PBSetForeignPrivs"},

  // _SCSIDispatch (0x0815) - subroutine number passed via stack (word)
  {make_pack_trap_id(0x0815,  0), "SCSIReset"},
  {make_pack_trap_id(0x0815,  1), "SCSIGet"},
  {make_pack_trap_id(0x0815,  2), "SCSISelect"},
  {make_pack_trap_id(0x0815,  3), "SCSICmd"},
  {make_pack_trap_id(0x0815,  4), "SCSIComplete"},
  {make_pack_trap_id(0x0815,  5), "SCSIRead"},
  {make_pack_trap_id(0x0815,  6), "SCSIWrite"},
  {make_pack_trap_id(0x0815,  7), "SCSIInstall"},
  {make_pack_trap_id(0x0815,  8), "SCSIRBlind"},
  {make_pack_trap_id(0x0815,  9), "SCSIWBlind"},
  {make_pack_trap_id(0x0815, 10), "SCSIStat"},
  {make_pack_trap_id(0x0815, 11), "SCSISelAtn"},
  {make_pack_trap_id(0x0815, 12), "SCSIMsgIn"},
  {make_pack_trap_id(0x0815, 13), "SCSIMsgOut"},

  // _InternalWait (0x007F)
  {make_pack_trap_id(0x007F, 0), "SetTimeout"},
  {make_pack_trap_id(0x007F, 1), "GetTimeout"},

  // _ScriptUtil (0x08B5) - subroutine number passed via stack (long)
  {make_pack_trap_id(0x08B5,  0), "smFontScript"},
  {make_pack_trap_id(0x08B5,  2), "smIntlScript"},
  {make_pack_trap_id(0x08B5,  4), "smKybdScript"},
  {make_pack_trap_id(0x08B5,  6), "smFont2Script"},
  {make_pack_trap_id(0x08B5,  8), "smGetEnvirons"},
  {make_pack_trap_id(0x08B5, 10), "smSetEnvirons"},
  {make_pack_trap_id(0x08B5, 12), "smGetScript"},
  {make_pack_trap_id(0x08B5, 14), "smSetScript"},
  {make_pack_trap_id(0x08B5, 16), "smCharByte"},
  {make_pack_trap_id(0x08B5, 18), "smCharType"},
  {make_pack_trap_id(0x08B5, 20), "smPixel2Char"},
  {make_pack_trap_id(0x08B5, 22), "smChar2Pixel"},
  {make_pack_trap_id(0x08B5, 24), "smTranslit"},
  {make_pack_trap_id(0x08B5, 26), "smFindWord"},
  {make_pack_trap_id(0x08B5, 28), "smHiliteText"},
  {make_pack_trap_id(0x08B5, 30), "smDrawJust"},
  {make_pack_trap_id(0x08B5, 32), "smMeasureJust"},

  // _Shutdown (0x0895) - subroutine number passed via stack (word)
  {make_pack_trap_id(0x0895, 1), "ShutDwnPower"},
  {make_pack_trap_id(0x0895, 2), "ShutDwnStart"},
  {make_pack_trap_id(0x0895, 3), "ShutDwnInstall"},
  {make_pack_trap_id(0x0895, 4), "ShutDwnRemove"},

  // _SlotManager (0x006E) - subroutine number passed in D0
  {make_pack_trap_id(0x006E,  0), "sReadByte"},
  {make_pack_trap_id(0x006E,  1), "sReadWord"},
  {make_pack_trap_id(0x006E,  2), "sReadLong"},
  {make_pack_trap_id(0x006E,  3), "sGetcString"},
  {make_pack_trap_id(0x006E,  5), "sGetBlock"},
  {make_pack_trap_id(0x006E,  6), "sFindStruct"},
  {make_pack_trap_id(0x006E,  7), "sReadStruct"},
  {make_pack_trap_id(0x006E, 16), "sReadInfo"},
  {make_pack_trap_id(0x006E, 17), "sReadPRAMRec"},
  {make_pack_trap_id(0x006E, 18), "sPutPRAMRec"},
  {make_pack_trap_id(0x006E, 19), "sReadFHeader"},
  {make_pack_trap_id(0x006E, 20), "sNextRsrc"},
  {make_pack_trap_id(0x006E, 21), "sNextTypesRsrc"},
  {make_pack_trap_id(0x006E, 22), "sRsrcInfo"},
  {make_pack_trap_id(0x006E, 23), "sDisposePtr"},
  {make_pack_trap_id(0x006E, 24), "sCkCardStatus"},
  {make_pack_trap_id(0x006E, 25), "sReadDrvrName"},
  {make_pack_trap_id(0x006E, 27), "sFindDevBase"},
  {make_pack_trap_id(0x006E, 32), "InitSDec1Mgr"},
  {make_pack_trap_id(0x006E, 33), "sPrimaryInit"},
  {make_pack_trap_id(0x006E, 34), "sCardChanged"},
  {make_pack_trap_id(0x006E, 35), "sExec"},
  {make_pack_trap_id(0x006E, 36), "sOffsetData"},
  {make_pack_trap_id(0x006E, 37), "InitPRAMRecs"},
  {make_pack_trap_id(0x006E, 38), "sReadPBSize"},
  {make_pack_trap_id(0x006E, 40), "sCalcStep"},
  {make_pack_trap_id(0x006E, 41), "InitsRsrcTable"},
  {make_pack_trap_id(0x006E, 42), "sSearchSRT"},
  {make_pack_trap_id(0x006E, 43), "sUpdateSRT"},
  {make_pack_trap_id(0x006E, 44), "sCalcsPointer"},
  {make_pack_trap_id(0x006E, 45), "sGetDriver"},
  {make_pack_trap_id(0x006E, 46), "sPtrToSlot"},
  {make_pack_trap_id(0x006E, 47), "sFindsInfoRecPtr"},
  {make_pack_trap_id(0x006E, 48), "sFindsRsrcPtr"},
  {make_pack_trap_id(0x006E, 49), "sdeleteSRTRec"},
});

const char* name_for_68k_pack_trap(uint16_t parent_trap_num, uint32_t subroutine_num) {
  try {
    return pack_trap_names.at(make_pack_trap_id(parent_trap_num, subroutine_num));
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