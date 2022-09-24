#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <map>
#include <unordered_map>
#include <vector>

#include "QuickDrawFormats.hh"
#include "ExecutableFormats/PEFFile.hh"



enum class IndexFormat {
  NONE = 0, // For ResourceFiles constructed in memory
  RESOURCE_FORK,
  MOHAWK,
  HIRF,
  DC_DATA,
};

constexpr uint32_t resource_type(const char (&type)[5]) {
  return  (uint32_t(uint8_t(type[0])) << 24) |
          (uint32_t(uint8_t(type[1])) << 16) |
          (uint32_t(uint8_t(type[2])) << 8) |
          uint32_t(uint8_t(type[3]));
}
// Just to make sure the function works as intended
static_assert(resource_type("actb") == 0x61637462);

constexpr uint32_t RESOURCE_TYPE_actb = resource_type("actb");
constexpr uint32_t RESOURCE_TYPE_acur = resource_type("acur");
constexpr uint32_t RESOURCE_TYPE_ADBS = resource_type("ADBS");
constexpr uint32_t RESOURCE_TYPE_adio = resource_type("adio");
constexpr uint32_t RESOURCE_TYPE_AINI = resource_type("AINI");
constexpr uint32_t RESOURCE_TYPE_ALIS = resource_type("ALIS");
constexpr uint32_t RESOURCE_TYPE_alis = resource_type("alis");
constexpr uint32_t RESOURCE_TYPE_ALRT = resource_type("ALRT");
constexpr uint32_t RESOURCE_TYPE_APPL = resource_type("APPL");
constexpr uint32_t RESOURCE_TYPE_atlk = resource_type("atlk");
constexpr uint32_t RESOURCE_TYPE_audt = resource_type("audt");
constexpr uint32_t RESOURCE_TYPE_BNDL = resource_type("BNDL");
constexpr uint32_t RESOURCE_TYPE_boot = resource_type("boot");
constexpr uint32_t RESOURCE_TYPE_bstr = resource_type("bstr");
constexpr uint32_t RESOURCE_TYPE_card = resource_type("card");
constexpr uint32_t RESOURCE_TYPE_cctb = resource_type("cctb");
constexpr uint32_t RESOURCE_TYPE_CDEF = resource_type("CDEF");
constexpr uint32_t RESOURCE_TYPE_cdek = resource_type("cdek");
constexpr uint32_t RESOURCE_TYPE_cdev = resource_type("cdev");
constexpr uint32_t RESOURCE_TYPE_cfrg = resource_type("cfrg");
constexpr uint32_t RESOURCE_TYPE_cicn = resource_type("cicn");
constexpr uint32_t RESOURCE_TYPE_citt = resource_type("citt");
constexpr uint32_t RESOURCE_TYPE_clok = resource_type("clok");
constexpr uint32_t RESOURCE_TYPE_clut = resource_type("clut");
constexpr uint32_t RESOURCE_TYPE_CMDK = resource_type("CMDK");
constexpr uint32_t RESOURCE_TYPE_cmid = resource_type("cmid");
constexpr uint32_t RESOURCE_TYPE_CMNU = resource_type("CMNU");
constexpr uint32_t RESOURCE_TYPE_cmnu = resource_type("cmnu");
constexpr uint32_t RESOURCE_TYPE_cmtb = resource_type("cmtb");
constexpr uint32_t RESOURCE_TYPE_cmuN = resource_type("cmu#");
constexpr uint32_t RESOURCE_TYPE_CNTL = resource_type("CNTL");
constexpr uint32_t RESOURCE_TYPE_CODE = resource_type("CODE");
constexpr uint32_t RESOURCE_TYPE_code = resource_type("code");
constexpr uint32_t RESOURCE_TYPE_crsr = resource_type("crsr");
constexpr uint32_t RESOURCE_TYPE_csnd = resource_type("csnd");
constexpr uint32_t RESOURCE_TYPE_CTBL = resource_type("CTBL");
constexpr uint32_t RESOURCE_TYPE_CTYN = resource_type("CTY#");
constexpr uint32_t RESOURCE_TYPE_CURS = resource_type("CURS");
constexpr uint32_t RESOURCE_TYPE_dbex = resource_type("dbex");
constexpr uint32_t RESOURCE_TYPE_dcmp = resource_type("dcmp");
constexpr uint32_t RESOURCE_TYPE_dcod = resource_type("dcod");
constexpr uint32_t RESOURCE_TYPE_dctb = resource_type("dctb");
constexpr uint32_t RESOURCE_TYPE_dem  = resource_type("dem ");
constexpr uint32_t RESOURCE_TYPE_dimg = resource_type("dimg");
constexpr uint32_t RESOURCE_TYPE_DITL = resource_type("DITL");
constexpr uint32_t RESOURCE_TYPE_DLOG = resource_type("DLOG");
constexpr uint32_t RESOURCE_TYPE_DRVR = resource_type("DRVR");
constexpr uint32_t RESOURCE_TYPE_drvr = resource_type("drvr");
constexpr uint32_t RESOURCE_TYPE_ecmi = resource_type("ecmi");
constexpr uint32_t RESOURCE_TYPE_emid = resource_type("emid");
constexpr uint32_t RESOURCE_TYPE_enet = resource_type("enet");
constexpr uint32_t RESOURCE_TYPE_epch = resource_type("epch");
constexpr uint32_t RESOURCE_TYPE_errs = resource_type("errs");
constexpr uint32_t RESOURCE_TYPE_ESnd = resource_type("ESnd");
constexpr uint32_t RESOURCE_TYPE_esnd = resource_type("esnd");
constexpr uint32_t RESOURCE_TYPE_expt = resource_type("expt");
constexpr uint32_t RESOURCE_TYPE_FBTN = resource_type("FBTN");
constexpr uint32_t RESOURCE_TYPE_FCMT = resource_type("FCMT");
constexpr uint32_t RESOURCE_TYPE_fctb = resource_type("fctb");
constexpr uint32_t RESOURCE_TYPE_FDIR = resource_type("FDIR");
constexpr uint32_t RESOURCE_TYPE_finf = resource_type("finf");
constexpr uint32_t RESOURCE_TYPE_FKEY = resource_type("FKEY");
constexpr uint32_t RESOURCE_TYPE_fldN = resource_type("fld#");
constexpr uint32_t RESOURCE_TYPE_flst = resource_type("flst");
constexpr uint32_t RESOURCE_TYPE_fmap = resource_type("fmap");
constexpr uint32_t RESOURCE_TYPE_FONT = resource_type("FONT");
constexpr uint32_t RESOURCE_TYPE_fovr = resource_type("fovr");
constexpr uint32_t RESOURCE_TYPE_FREF = resource_type("FREF");
constexpr uint32_t RESOURCE_TYPE_FRSV = resource_type("FRSV");
constexpr uint32_t RESOURCE_TYPE_FWID = resource_type("FWID");
constexpr uint32_t RESOURCE_TYPE_gbly = resource_type("gbly");
constexpr uint32_t RESOURCE_TYPE_gcko = resource_type("gcko");
constexpr uint32_t RESOURCE_TYPE_GDEF = resource_type("GDEF");
constexpr uint32_t RESOURCE_TYPE_gdef = resource_type("gdef");
constexpr uint32_t RESOURCE_TYPE_gnld = resource_type("gnld");
constexpr uint32_t RESOURCE_TYPE_GNRL = resource_type("GNRL");
constexpr uint32_t RESOURCE_TYPE_gpch = resource_type("gpch");
constexpr uint32_t RESOURCE_TYPE_h8mk = resource_type("h8mk");
constexpr uint32_t RESOURCE_TYPE_hqda = resource_type("hqda");
constexpr uint32_t RESOURCE_TYPE_hwin = resource_type("hwin");
constexpr uint32_t RESOURCE_TYPE_ic04 = resource_type("ic04");
constexpr uint32_t RESOURCE_TYPE_ic05 = resource_type("ic05");
constexpr uint32_t RESOURCE_TYPE_ic07 = resource_type("ic07");
constexpr uint32_t RESOURCE_TYPE_ic08 = resource_type("ic08");
constexpr uint32_t RESOURCE_TYPE_ic09 = resource_type("ic09");
constexpr uint32_t RESOURCE_TYPE_ic10 = resource_type("ic10");
constexpr uint32_t RESOURCE_TYPE_ic11 = resource_type("ic11");
constexpr uint32_t RESOURCE_TYPE_ic12 = resource_type("ic12");
constexpr uint32_t RESOURCE_TYPE_ic13 = resource_type("ic13");
constexpr uint32_t RESOURCE_TYPE_ic14 = resource_type("ic14");
constexpr uint32_t RESOURCE_TYPE_ich4 = resource_type("ich4");
constexpr uint32_t RESOURCE_TYPE_ich8 = resource_type("ich8");
constexpr uint32_t RESOURCE_TYPE_ichN = resource_type("ich#");
constexpr uint32_t RESOURCE_TYPE_icl4 = resource_type("icl4");
constexpr uint32_t RESOURCE_TYPE_icl8 = resource_type("icl8");
constexpr uint32_t RESOURCE_TYPE_icm4 = resource_type("icm4");
constexpr uint32_t RESOURCE_TYPE_icm8 = resource_type("icm8");
constexpr uint32_t RESOURCE_TYPE_icmN = resource_type("icm#");
constexpr uint32_t RESOURCE_TYPE_icmt = resource_type("icmt");
constexpr uint32_t RESOURCE_TYPE_ICNN = resource_type("ICN#");
constexpr uint32_t RESOURCE_TYPE_icns = resource_type("icns");
constexpr uint32_t RESOURCE_TYPE_icnV = resource_type("icnV");
constexpr uint32_t RESOURCE_TYPE_ICON = resource_type("ICON");
constexpr uint32_t RESOURCE_TYPE_icp4 = resource_type("icp4");
constexpr uint32_t RESOURCE_TYPE_icp5 = resource_type("icp5");
constexpr uint32_t RESOURCE_TYPE_icp6 = resource_type("icp6");
constexpr uint32_t RESOURCE_TYPE_ics4 = resource_type("ics4");
constexpr uint32_t RESOURCE_TYPE_ics8 = resource_type("ics8");
constexpr uint32_t RESOURCE_TYPE_icsb = resource_type("icsb");
constexpr uint32_t RESOURCE_TYPE_icsB = resource_type("icsB");
constexpr uint32_t RESOURCE_TYPE_icsN = resource_type("ics#");
constexpr uint32_t RESOURCE_TYPE_ih32 = resource_type("ih32");
constexpr uint32_t RESOURCE_TYPE_il32 = resource_type("il32");
constexpr uint32_t RESOURCE_TYPE_inbb = resource_type("inbb");
constexpr uint32_t RESOURCE_TYPE_indm = resource_type("indm");
constexpr uint32_t RESOURCE_TYPE_info = resource_type("info");
constexpr uint32_t RESOURCE_TYPE_infs = resource_type("infs");
constexpr uint32_t RESOURCE_TYPE_INIT = resource_type("INIT");
constexpr uint32_t RESOURCE_TYPE_inpk = resource_type("inpk");
constexpr uint32_t RESOURCE_TYPE_inra = resource_type("inra");
constexpr uint32_t RESOURCE_TYPE_insc = resource_type("insc");
constexpr uint32_t RESOURCE_TYPE_INST = resource_type("INST");
constexpr uint32_t RESOURCE_TYPE_is32 = resource_type("is32");
constexpr uint32_t RESOURCE_TYPE_it32 = resource_type("it32");
constexpr uint32_t RESOURCE_TYPE_itl0 = resource_type("itl0");
constexpr uint32_t RESOURCE_TYPE_ITL1 = resource_type("ITL1");
constexpr uint32_t RESOURCE_TYPE_itlb = resource_type("itlb");
constexpr uint32_t RESOURCE_TYPE_itlc = resource_type("itlc");
constexpr uint32_t RESOURCE_TYPE_itlk = resource_type("itlk");
constexpr uint32_t RESOURCE_TYPE_KBDN = resource_type("KBDN");
constexpr uint32_t RESOURCE_TYPE_kcs4 = resource_type("kcs4");
constexpr uint32_t RESOURCE_TYPE_kcs8 = resource_type("kcs8");
constexpr uint32_t RESOURCE_TYPE_kcsN = resource_type("kcs#");
constexpr uint32_t RESOURCE_TYPE_krnl = resource_type("krnl");
constexpr uint32_t RESOURCE_TYPE_l8mk = resource_type("l8mk");
constexpr uint32_t RESOURCE_TYPE_LAYO = resource_type("LAYO");
constexpr uint32_t RESOURCE_TYPE_LDEF = resource_type("LDEF");
constexpr uint32_t RESOURCE_TYPE_lmgr = resource_type("lmgr");
constexpr uint32_t RESOURCE_TYPE_lodr = resource_type("lodr");
constexpr uint32_t RESOURCE_TYPE_lstr = resource_type("lstr");
constexpr uint32_t RESOURCE_TYPE_ltlk = resource_type("ltlk");
constexpr uint32_t RESOURCE_TYPE_mach = resource_type("mach");
constexpr uint32_t RESOURCE_TYPE_MACS = resource_type("MACS");
constexpr uint32_t RESOURCE_TYPE_MADH = resource_type("MADH");
constexpr uint32_t RESOURCE_TYPE_MADI = resource_type("MADI");
constexpr uint32_t RESOURCE_TYPE_MBAR = resource_type("MBAR");
constexpr uint32_t RESOURCE_TYPE_MBDF = resource_type("MBDF");
constexpr uint32_t RESOURCE_TYPE_mcky = resource_type("mcky");
constexpr uint32_t RESOURCE_TYPE_MDEF = resource_type("MDEF");
constexpr uint32_t RESOURCE_TYPE_MENU = resource_type("MENU");
constexpr uint32_t RESOURCE_TYPE_MIDI = resource_type("MIDI");
constexpr uint32_t RESOURCE_TYPE_Midi = resource_type("Midi");
constexpr uint32_t RESOURCE_TYPE_midi = resource_type("midi");
constexpr uint32_t RESOURCE_TYPE_minf = resource_type("minf");
constexpr uint32_t RESOURCE_TYPE_mitq = resource_type("mitq");
constexpr uint32_t RESOURCE_TYPE_mntr = resource_type("mntr");
constexpr uint32_t RESOURCE_TYPE_MOOV = resource_type("MOOV");
constexpr uint32_t RESOURCE_TYPE_MooV = resource_type("MooV");
constexpr uint32_t RESOURCE_TYPE_moov = resource_type("moov");
constexpr uint32_t RESOURCE_TYPE_mstr = resource_type("mstr");
constexpr uint32_t RESOURCE_TYPE_mstN = resource_type("mst#");
constexpr uint32_t RESOURCE_TYPE_name = resource_type("name");
constexpr uint32_t RESOURCE_TYPE_ncmp = resource_type("ncmp");
constexpr uint32_t RESOURCE_TYPE_ndlc = resource_type("ndlc");
constexpr uint32_t RESOURCE_TYPE_ndmc = resource_type("ndmc");
constexpr uint32_t RESOURCE_TYPE_ndrv = resource_type("ndrv");
constexpr uint32_t RESOURCE_TYPE_NFNT = resource_type("NFNT");
constexpr uint32_t RESOURCE_TYPE_nift = resource_type("nift");
constexpr uint32_t RESOURCE_TYPE_nitt = resource_type("nitt");
constexpr uint32_t RESOURCE_TYPE_nlib = resource_type("nlib");
constexpr uint32_t RESOURCE_TYPE_nrct = resource_type("nrct");
constexpr uint32_t RESOURCE_TYPE_nsnd = resource_type("nsnd");
constexpr uint32_t RESOURCE_TYPE_nsrd = resource_type("nsrd");
constexpr uint32_t RESOURCE_TYPE_ntrb = resource_type("ntrb");
constexpr uint32_t RESOURCE_TYPE_osl  = resource_type("osl ");
constexpr uint32_t RESOURCE_TYPE_otdr = resource_type("otdr");
constexpr uint32_t RESOURCE_TYPE_otlm = resource_type("otlm");
constexpr uint32_t RESOURCE_TYPE_PACK = resource_type("PACK");
constexpr uint32_t RESOURCE_TYPE_PAPA = resource_type("PAPA");
constexpr uint32_t RESOURCE_TYPE_PAT  = resource_type("PAT ");
constexpr uint32_t RESOURCE_TYPE_PATN = resource_type("PAT#");
constexpr uint32_t RESOURCE_TYPE_PICK = resource_type("PICK");
constexpr uint32_t RESOURCE_TYPE_PICT = resource_type("PICT");
constexpr uint32_t RESOURCE_TYPE_pltt = resource_type("pltt");
constexpr uint32_t RESOURCE_TYPE_pnll = resource_type("pnll");
constexpr uint32_t RESOURCE_TYPE_ppat = resource_type("ppat");
constexpr uint32_t RESOURCE_TYPE_ppcc = resource_type("ppcc");
constexpr uint32_t RESOURCE_TYPE_ppci = resource_type("ppci");
constexpr uint32_t RESOURCE_TYPE_ppct = resource_type("ppct");
constexpr uint32_t RESOURCE_TYPE_PPic = resource_type("PPic");
constexpr uint32_t RESOURCE_TYPE_pptN = resource_type("ppt#");
constexpr uint32_t RESOURCE_TYPE_PRC0 = resource_type("PRC0");
constexpr uint32_t RESOURCE_TYPE_PRC3 = resource_type("PRC3");
constexpr uint32_t RESOURCE_TYPE_proc = resource_type("proc");
constexpr uint32_t RESOURCE_TYPE_PSAP = resource_type("PSAP");
constexpr uint32_t RESOURCE_TYPE_pslt = resource_type("pslt");
constexpr uint32_t RESOURCE_TYPE_ptbl = resource_type("ptbl");
constexpr uint32_t RESOURCE_TYPE_PTCH = resource_type("PTCH");
constexpr uint32_t RESOURCE_TYPE_ptch = resource_type("ptch");
constexpr uint32_t RESOURCE_TYPE_pthg = resource_type("pthg");
constexpr uint32_t RESOURCE_TYPE_qrsc = resource_type("qrsc");
constexpr uint32_t RESOURCE_TYPE_qtcm = resource_type("qtcm");
constexpr uint32_t RESOURCE_TYPE_RECT = resource_type("RECT");
constexpr uint32_t RESOURCE_TYPE_resf = resource_type("resf");
constexpr uint32_t RESOURCE_TYPE_RMAP = resource_type("RMAP");
constexpr uint32_t RESOURCE_TYPE_ROvN = resource_type("ROv#");
constexpr uint32_t RESOURCE_TYPE_ROvr = resource_type("ROvr");
constexpr uint32_t RESOURCE_TYPE_rttN = resource_type("rtt#");
constexpr uint32_t RESOURCE_TYPE_RVEW = resource_type("RVEW");
constexpr uint32_t RESOURCE_TYPE_s8mk = resource_type("s8mk");
constexpr uint32_t RESOURCE_TYPE_sb24 = resource_type("sb24");
constexpr uint32_t RESOURCE_TYPE_SB24 = resource_type("SB24");
constexpr uint32_t RESOURCE_TYPE_sbtp = resource_type("sbtp");
constexpr uint32_t RESOURCE_TYPE_scal = resource_type("scal");
constexpr uint32_t RESOURCE_TYPE_scod = resource_type("scod");
constexpr uint32_t RESOURCE_TYPE_scrn = resource_type("scrn");
constexpr uint32_t RESOURCE_TYPE_sect = resource_type("sect");
constexpr uint32_t RESOURCE_TYPE_SERD = resource_type("SERD");
constexpr uint32_t RESOURCE_TYPE_sfnt = resource_type("sfnt");
constexpr uint32_t RESOURCE_TYPE_sfvr = resource_type("sfvr");
constexpr uint32_t RESOURCE_TYPE_shal = resource_type("shal");
constexpr uint32_t RESOURCE_TYPE_SICN = resource_type("SICN");
constexpr uint32_t RESOURCE_TYPE_sift = resource_type("sift");
constexpr uint32_t RESOURCE_TYPE_SIGN = resource_type("SIGN");
constexpr uint32_t RESOURCE_TYPE_SIZE = resource_type("SIZE");
constexpr uint32_t RESOURCE_TYPE_slct = resource_type("slct");
constexpr uint32_t RESOURCE_TYPE_slut = resource_type("slut");
constexpr uint32_t RESOURCE_TYPE_SMOD = resource_type("SMOD");
constexpr uint32_t RESOURCE_TYPE_SMSD = resource_type("SMSD");
constexpr uint32_t RESOURCE_TYPE_snd  = resource_type("snd ");
constexpr uint32_t RESOURCE_TYPE_snth = resource_type("snth");
constexpr uint32_t RESOURCE_TYPE_SONG = resource_type("SONG");
constexpr uint32_t RESOURCE_TYPE_SOUN = resource_type("SOUN");
constexpr uint32_t RESOURCE_TYPE_STR  = resource_type("STR ");
constexpr uint32_t RESOURCE_TYPE_STRN = resource_type("STR#");
constexpr uint32_t RESOURCE_TYPE_styl = resource_type("styl");
constexpr uint32_t RESOURCE_TYPE_t8mk = resource_type("t8mk");
constexpr uint32_t RESOURCE_TYPE_tdig = resource_type("tdig");
constexpr uint32_t RESOURCE_TYPE_TEXT = resource_type("TEXT");
constexpr uint32_t RESOURCE_TYPE_thnN = resource_type("thn#");
constexpr uint32_t RESOURCE_TYPE_TMPL = resource_type("TMPL");
constexpr uint32_t RESOURCE_TYPE_TOC  = resource_type("TOC ");
constexpr uint32_t RESOURCE_TYPE_tokn = resource_type("tokn");
constexpr uint32_t RESOURCE_TYPE_TOOL = resource_type("TOOL");
constexpr uint32_t RESOURCE_TYPE_Tune = resource_type("Tune");
constexpr uint32_t RESOURCE_TYPE_vdig = resource_type("vdig");
constexpr uint32_t RESOURCE_TYPE_vers = resource_type("vers");
constexpr uint32_t RESOURCE_TYPE_wart = resource_type("wart");
constexpr uint32_t RESOURCE_TYPE_wctb = resource_type("wctb");
constexpr uint32_t RESOURCE_TYPE_WDEF = resource_type("WDEF");
constexpr uint32_t RESOURCE_TYPE_WIND = resource_type("WIND");
constexpr uint32_t RESOURCE_TYPE_wstr = resource_type("wstr");
constexpr uint32_t RESOURCE_TYPE_XCMD = resource_type("XCMD");
constexpr uint32_t RESOURCE_TYPE_XFCN = resource_type("XFCN");
constexpr uint32_t RESOURCE_TYPE_Ysnd = resource_type("Ysnd");

std::string string_for_resource_type(uint32_t type);
std::string raw_string_for_resource_type(uint32_t type);



enum ResourceFlag {
  // The low 8 bits come from the resource itself; the high 8 bits are reserved
  // for resource_dasm
  FLAG_DECOMPRESSED = 0x0200, // decompressor ran successfully
  FLAG_DECOMPRESSION_FAILED = 0x0100, // so we don't try to decompress again
  FLAG_LOAD_IN_SYSTEM_HEAP = 0x0040,
  FLAG_PURGEABLE = 0x0020,
  FLAG_LOCKED = 0x0010,
  FLAG_PROTECTED = 0x0008,
  FLAG_PRELOAD = 0x0004,
  FLAG_DIRTY = 0x0002, // only used while loaded; set if needs to be written to disk
  FLAG_COMPRESSED = 0x0001,
};



class ResourceFile {
public:
  // This class defines the loaded representation of a resource archive, and
  // includes functions to decode resources and add/remove/change the archive
  // contents. To parse an existing archive and get a ResourceFile object, use a
  // function defined in one of the headers in the IndexFormats directory. The
  // constructors defined in this class will only create an empty ResourceFile.

  ResourceFile();
  explicit ResourceFile(IndexFormat format);
  ResourceFile(const ResourceFile&) = default;
  ResourceFile(ResourceFile&&) = default;
  ResourceFile& operator=(const ResourceFile&) = default;
  ResourceFile& operator=(ResourceFile&&) = default;
  ~ResourceFile() = default;

  struct Resource {
    uint32_t type;
    int16_t id;
    uint16_t flags; // bits from ResourceFlag enum
    std::string name;
    std::string data;

    Resource();
    Resource(const Resource&) = default;
    Resource(Resource&&) = default;
    Resource& operator=(const Resource&) = default;
    Resource& operator=(Resource&&) = default;
    Resource(uint32_t type, int16_t id, const std::string& data);
    Resource(uint32_t type, int16_t id, std::string&& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, const std::string& name, const std::string& data);
    Resource(uint32_t type, int16_t id, uint16_t flags, std::string&& name, std::string&& data);
  };

  // add() does not overwrite a resource if one already exists with the same
  // name. To replace an existing resource, remove() it first. (Note that
  // remove() will invalidate all references to the deleted resource that were
  // previously returned by get_resource().)
  bool add(const Resource& res);
  bool add(Resource&& res);
  bool add(std::shared_ptr<Resource> res);
  bool remove(uint32_t type, int16_t id);
  bool change_id(uint32_t type, int16_t current_id, int16_t new_id);
  bool rename(uint32_t type, int16_t id, const std::string& new_name);

  IndexFormat index_format() const;

  bool resource_exists(uint32_t type, int16_t id) const;
  bool resource_exists(uint32_t type, const char* name) const;
  std::shared_ptr<Resource> get_resource(
      uint32_t type, int16_t id, uint64_t decompression_flags = 0);
  std::shared_ptr<Resource> get_resource(
      uint32_t type, const char* name, uint64_t decompression_flags = 0);
  // Warning: The const versions of get_resource do not decompress resources
  // automatically! They are essentially equivalent to the non-const versions
  // with decompression_flags = DecompressionFlag::DISABLED.
  std::shared_ptr<const Resource> get_resource(uint32_t type, int16_t id) const;
  std::shared_ptr<const Resource> get_resource(uint32_t type, const char* name) const;
  std::vector<int16_t> all_resources_of_type(uint32_t type) const;
  std::vector<uint32_t> all_resource_types() const;
  std::vector<std::pair<uint32_t, int16_t>> all_resources() const;

  uint32_t find_resource_by_id(int16_t id, const std::vector<uint32_t>& types);

  struct DecodedCodeFragmentEntry {
    uint32_t architecture;
    uint8_t update_level;
    uint32_t current_version;
    uint32_t old_def_version;
    uint32_t app_stack_size;
    union {
      int16_t app_subdir_id;
      uint16_t lib_flags;
    };

    enum class Usage {
      IMPORT_LIBRARY = 0,
      APPLICATION = 1,
      DROP_IN_ADDITION = 2,
      STUB_LIBRARY = 3,
      WEAK_STUB_LIBRARY = 4,
    };
    Usage usage;

    enum class Where {
      MEMORY = 0,
      DATA_FORK = 1,
      RESOURCE = 2,
      BYTE_STREAM = 3, // reserved
      NAMED_FRAGMENT = 4, // reserved
    };
    Where where;

    uint32_t offset;
    uint32_t length; // if zero, fragment fills the entire space (e.g. entire data fork)
    union {
      uint32_t space_id;
      uint32_t fork_kind;
    };
    uint16_t fork_instance;
    std::string name;
    // TODO: support extensions
    uint16_t extension_count;
    std::string extension_data;
  };

  struct DecodedColorIconResource {
    Image image;
    Image bitmap;
  };

  struct DecodedIconListResource {
    // .empty() will be true for exactly one of these in all cases.
    // Specifically, if there are two icons in the resource, it is assumed that
    // they are a bitmap and mask (respectively) and they are combined into
    // .composite; for any other number of images, no compositing is done and
    // the images are decoded individually and put into .images.
    Image composite;
    std::vector<Image> images;
  };

  struct DecodedIconImagesResource {
    std::unordered_multimap<uint32_t, Image> type_to_image;
    std::unordered_multimap<uint32_t, Image> type_to_composite_image;
    std::unordered_multimap<uint32_t, std::string> type_to_jpeg2000_data;
    std::unordered_multimap<uint32_t, std::string> type_to_png_data;
    std::string toc_data;
    float icon_composer_version;
    std::string name;
    std::string info_plist;
    std::shared_ptr<DecodedIconImagesResource> template_icns;
    std::shared_ptr<DecodedIconImagesResource> selected_icns;
    std::shared_ptr<DecodedIconImagesResource> dark_icns;

    DecodedIconImagesResource();
  };

  struct DecodedCursorResource {
    Image bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
  };

  struct DecodedColorCursorResource {
    Image image;
    Image bitmap;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
  };

  struct DecodedSoundResource {
    bool is_mp3;
    uint32_t sample_rate;
    uint8_t base_note;
    // This string contains a raw WAV or MP3 file (determined by is_mp3)
    std::string data;
  };

  struct DecodedInstrumentResource {
    struct KeyRegion {
      uint8_t key_low;
      uint8_t key_high;
      uint8_t base_note;
      int16_t snd_id;
      uint32_t snd_type; // can be RESOURCE_TYPE_snd or RESOURCE_TYPE_csnd

      KeyRegion(uint8_t key_low, uint8_t key_high, uint8_t base_note,
          int16_t snd_id, uint32_t snd_type);
    };

    std::vector<KeyRegion> key_regions;
    uint8_t base_note;
    bool use_sample_rate;
    bool constant_pitch;
    std::vector<uint16_t> tremolo_data;
    std::string copyright;
    std::string author;
  };

  struct DecodedSongResource {
    bool is_rmf;
    int16_t midi_id;
    // 0 = private, 1 = RMF structured, 2 = RMF linear, -1 = standard MIDI (SMS)
    int16_t midi_format;
    uint16_t tempo_bias; // base = 16667; linear
    uint16_t volume_bias; // base = 127; linear
    int16_t semitone_shift;
    int16_t percussion_instrument; // -1 = unspecified (RMF)
    bool allow_program_change;
    std::unordered_map<uint16_t, uint16_t> instrument_overrides;

    std::string copyright_text;
    std::string composer; // Called "author" in SMS-format songs

    // The following fields are only present in RMF-format songs, and all are
    // optional.
    std::vector<uint16_t> velocity_override_map;
    std::string title;
    std::string performer;
    std::string copyright_date;
    std::string license_contact;
    std::string license_uses;
    std::string license_domain;
    std::string license_term;
    std::string license_expiration;
    std::string note;
    std::string index_number;
    std::string genre;
    std::string subgenre;
  };

  struct DecodedPattern {
    Image pattern;
    Image monochrome_pattern;
  };

  struct DecodedString {
    std::string str;
    std::string after_data;
  };

  struct DecodedStringSequence {
    std::vector<std::string> strs;
    std::string after_data;
  };

  struct DecodedCode0Resource {
    uint32_t above_a5_size;
    uint32_t below_a5_size;

    struct JumpTableEntry {
      int16_t code_resource_id; // entry not valid if this is zero
      uint16_t offset; // offset from end of CODE resource header
    };
    std::vector<JumpTableEntry> jump_table;
  };

  struct DecodedCodeResource {
    // if near model, this is >= 0 and the far model fields are uninitialized:
    int32_t first_jump_table_entry;
    uint16_t num_jump_table_entries;

    // if far model, entry_offset is < 0 and these will all be initialized:
    uint32_t near_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t near_entry_count;
    uint32_t far_entry_start_a5_offset; // offset from A5, so subtract 0x20
    uint32_t far_entry_count;
    uint32_t a5_relocation_data_offset;
    uint32_t a5;
    uint32_t pc_relocation_data_offset;
    uint32_t load_address; // unintuitive; see docs

    std::vector<uint32_t> a5_relocation_addresses;
    std::vector<uint32_t> pc_relocation_addresses;

    std::string code;
  };

  struct DecodedDriverResource {
    enum Flag {
      ENABLE_READ = 0x0100,
      ENABLE_WRITE = 0x0200,
      ENABLE_CONTROL = 0x0400,
      ENABLE_STATUS = 0x0800,
      NEED_GOODBYE = 0x1000,
      NEED_TIME = 0x2000,
      NEED_LOCK = 0x4000,
    };
    uint16_t flags;
    uint16_t delay;
    uint16_t event_mask;
    int16_t menu_id;
    // If any of these are -1, the label is missing
    int32_t open_label;
    int32_t prime_label;
    int32_t control_label;
    int32_t status_label;
    int32_t close_label;
    std::string name;
    std::string code;
  };

  struct DecodedDecompressorResource {
    int32_t init_label;
    int32_t decompress_label;
    int32_t exit_label;
    uint32_t pc_offset;
    std::string code;
  };

  struct DecodedSizeResource {
    bool save_screen;
    bool accept_suspend_events;
    bool disable_option;
    bool can_background;
    bool activate_on_fg_switch;
    bool only_background;
    bool get_front_clicks;
    bool accept_died_events;
    bool clean_addressing; // "32-bit compatible"
    bool high_level_event_aware;
    bool local_and_remote_high_level_events;
    bool stationery_aware;
    bool use_text_edit_services;
    uint32_t size;
    uint32_t min_size;
  };

  struct DecodedVersionResource {
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t development_stage;
    uint8_t prerelease_version_level;
    uint16_t region_code;
    std::string version_number;
    std::string version_message;
  };

  struct DecodedPictResource {
    Image image;
    std::string embedded_image_format;
    std::string embedded_image_data;
  };

  struct DecodedFontResource {
    uint8_t source_bit_depth;
    std::vector<ColorTableEntry> color_table;
    bool is_dynamic;
    bool has_non_black_colors;
    bool fixed_width;
    uint16_t first_char;
    uint16_t last_char;
    uint16_t max_width;
    int16_t max_kerning;
    uint16_t rect_width;
    uint16_t rect_height;
    int16_t max_ascent;
    int16_t max_descent;
    int16_t leading;

    struct Glyph {
      int16_t ch;
      uint16_t bitmap_offset;
      uint16_t bitmap_width;
      int8_t offset;
      uint8_t width;
      Image img;
    };
    Glyph missing_glyph;
    std::vector<Glyph> glyphs;
  };

  enum TextStyleFlag {
    BOLD = 0x01,
    ITALIC = 0x02,
    UNDERLINE = 0x04,
    OUTLINE = 0x08,
    SHADOW = 0x10,
    CONDENSED = 0x20,
    EXTENDED = 0x40,
  };

  struct DecodedFontInfo {
    uint16_t font_id;
    uint16_t style_flags;
    uint16_t size;
  };

  struct DecodedROMOverride {
    be_uint32_t type;
    be_int16_t id;
  } __attribute__((packed));

  struct DecodedROMOverridesResource {
    uint16_t rom_version;
    std::vector<DecodedROMOverride> overrides;
  };

  struct DecodedPEFDriver {
    std::string header;
    PEFFile pef;
  };

  struct TemplateEntry {
    enum class Type {
      VOID, // DVDR
      INTEGER, // DBYT, BWRD, DLNG, HBYT, HWRD, HLNG, CHAR, TNAM
      ALIGNMENT, // AWORD, ALNG
      ZERO_FILL, // FBYT, FWRD, FLNG
      EOF_STRING, // HEXD
      FIXED_POINT, // FIXD (.width is number of bytes for each field)
      POINT_2D, // 'PNT ' (.width is number of bytes for each dimension)
      STRING, // Hxxx (.width is number of bytes)
      PSTRING, // PSTR, WSTR, LSTR, ESTR, OSTR (.width is width of length field)
      CSTRING, // CSTR, ECST, OCST
      FIXED_PSTRING, // P0xx (length byte not included in xx)
      FIXED_CSTRING, // Cxxx
      BOOL, // BOOL (two bytes, for some reason...)
      BITFIELD, // BBIT (list_entries contains 8 BOOL entries)
      RECT, // RECT (.width is width of each field)
      COLOR, // COLR (.width is the width of each channel)
      LIST_ZERO_BYTE, // LSTZ+LSTE
      LIST_ZERO_COUNT, // ZCNT+LSTC+LSTE (count is a word)
      LIST_ONE_COUNT, // OCNT+LSTC+LSTE (count is a word)
      LIST_EOF, // LSTB+LSTE
      OPT_EOF, // If there's still data left
    };
    enum class Format {
      DECIMAL,
      HEX,
      TEXT, // For integers, this results in hex+text
      FLAG,
      DATE,
    };

    std::string name;
    Type type;
    Format format;
    uint16_t width;
    uint8_t end_alignment;
    uint8_t align_offset; // 1 for odd-aligned strings, for example
    bool is_signed;

    std::vector<std::shared_ptr<TemplateEntry>> list_entries;
    std::map<int64_t, std::string> case_names;

    TemplateEntry(std::string&& name,
        Type type,
        Format format = Format::DECIMAL,
        uint16_t width = 0,
        uint8_t end_alignment = 0,
        uint8_t align_offset = 0,
        bool is_signed = true,
        std::map<int64_t, std::string> case_names = {});
    TemplateEntry(std::string&& name,
        Type type,
        std::vector<std::shared_ptr<TemplateEntry>>&& list_entries);
  };
  using TemplateEntryList = std::vector<std::shared_ptr<ResourceFile::TemplateEntry>>;

  // Meta resources
  TemplateEntryList decode_TMPL(int16_t id, uint32_t type = RESOURCE_TYPE_TMPL);
  static TemplateEntryList decode_TMPL(std::shared_ptr<const Resource> res);
  static TemplateEntryList decode_TMPL(const void* data, size_t size);

  static std::string disassemble_from_template(
      const void* data, size_t size, const TemplateEntryList& tmpl);

  // Code metadata resources
  DecodedSizeResource decode_SIZE(int16_t id, uint32_t type = RESOURCE_TYPE_SIZE);
  static DecodedSizeResource decode_SIZE(std::shared_ptr<const Resource> res);
  static DecodedSizeResource decode_SIZE(const void* data, size_t size);
  DecodedVersionResource decode_vers(int16_t id, uint32_t type = RESOURCE_TYPE_vers);
  static DecodedVersionResource decode_vers(std::shared_ptr<const Resource> res);
  static DecodedVersionResource decode_vers(const void* data, size_t size);
  std::vector<DecodedCodeFragmentEntry> decode_cfrg(int16_t id, uint32_t type = RESOURCE_TYPE_cfrg);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(std::shared_ptr<const Resource> res);
  static std::vector<DecodedCodeFragmentEntry> decode_cfrg(const void* vdata, size_t size);
  DecodedROMOverridesResource decode_ROvN(int16_t id, uint32_t type = RESOURCE_TYPE_ROvN);
  static DecodedROMOverridesResource decode_ROvN(std::shared_ptr<const Resource> res);
  static DecodedROMOverridesResource decode_ROvN(const void* data, size_t size);

  // 68K code resources
  DecodedCode0Resource decode_CODE_0(int16_t id = 0, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCode0Resource decode_CODE_0(std::shared_ptr<const Resource> res);
  static DecodedCode0Resource decode_CODE_0(const void* vdata, size_t size);
  DecodedCodeResource decode_CODE(int16_t id, uint32_t type = RESOURCE_TYPE_CODE);
  static DecodedCodeResource decode_CODE(std::shared_ptr<const Resource> res);
  static DecodedCodeResource decode_CODE(const void* vdata, size_t size);
  DecodedDriverResource decode_DRVR(int16_t id, uint32_t type = RESOURCE_TYPE_DRVR);
  static DecodedDriverResource decode_DRVR(std::shared_ptr<const Resource> res);
  static DecodedDriverResource decode_DRVR(const void* vdata, size_t size);
  DecodedDecompressorResource decode_dcmp(int16_t id, uint32_t type = RESOURCE_TYPE_dcmp);
  static DecodedDecompressorResource decode_dcmp(std::shared_ptr<const Resource> res);
  static DecodedDecompressorResource decode_dcmp(const void* vdata, size_t size);

  // PowerPC code resources
  PEFFile decode_pef(int16_t id, uint32_t type);
  static PEFFile decode_pef(std::shared_ptr<const Resource> res);
  static PEFFile decode_pef(const void* data, size_t size);
  DecodedPEFDriver decode_expt(int16_t id, uint32_t type = RESOURCE_TYPE_expt);
  static DecodedPEFDriver decode_expt(std::shared_ptr<const Resource> res);
  static DecodedPEFDriver decode_expt(const void* data, size_t size);
  DecodedPEFDriver decode_nsrd(int16_t id, uint32_t type = RESOURCE_TYPE_nsrd);
  static DecodedPEFDriver decode_nsrd(std::shared_ptr<const Resource> res);
  static DecodedPEFDriver decode_nsrd(const void* data, size_t size);

  // Image resources
  DecodedColorIconResource decode_cicn(int16_t id, uint32_t type = RESOURCE_TYPE_cicn);
  static DecodedColorIconResource decode_cicn(std::shared_ptr<const Resource> res);
  static DecodedColorIconResource decode_cicn(const void* vdata, size_t size);
  DecodedCursorResource decode_CURS(int16_t id, uint32_t type = RESOURCE_TYPE_CURS);
  static DecodedCursorResource decode_CURS(std::shared_ptr<const Resource> res);
  static DecodedCursorResource decode_CURS(const void* data, size_t size);
  DecodedColorCursorResource decode_crsr(int16_t id, uint32_t type = RESOURCE_TYPE_crsr);
  static DecodedColorCursorResource decode_crsr(std::shared_ptr<const Resource> res);
  static DecodedColorCursorResource decode_crsr(const void* data, size_t size);
  DecodedPattern decode_ppat(int16_t id, uint32_t type = RESOURCE_TYPE_ppat);
  static DecodedPattern decode_ppat(std::shared_ptr<const Resource> res);
  static DecodedPattern decode_ppat(const void* data, size_t size);
  std::vector<DecodedPattern> decode_pptN(int16_t id, uint32_t type = RESOURCE_TYPE_pptN);
  static std::vector<DecodedPattern> decode_pptN(std::shared_ptr<const Resource> res);
  static std::vector<DecodedPattern> decode_pptN(const void* data, size_t size);
  Image decode_PAT(int16_t id, uint32_t type = RESOURCE_TYPE_PAT);
  static Image decode_PAT(std::shared_ptr<const Resource> res);
  static Image decode_PAT(const void* data, size_t size);
  std::vector<Image> decode_PATN(int16_t id, uint32_t type = RESOURCE_TYPE_PATN);
  static std::vector<Image> decode_PATN(std::shared_ptr<const Resource> res);
  static std::vector<Image> decode_PATN(const void* data, size_t size);
  std::vector<Image> decode_SICN(int16_t id, uint32_t type = RESOURCE_TYPE_SICN);
  static std::vector<Image> decode_SICN(std::shared_ptr<const Resource> res);
  static std::vector<Image> decode_SICN(const void* data, size_t size);
  Image decode_icl8(int16_t id, uint32_t type = RESOURCE_TYPE_icl8);
  Image decode_icl8(std::shared_ptr<const Resource> res);
  static Image decode_icl8_without_alpha(const void* data, size_t size);
  Image decode_icm8(int16_t id, uint32_t type = RESOURCE_TYPE_icm8);
  Image decode_icm8(std::shared_ptr<const Resource> res);
  static Image decode_icm8_without_alpha(const void* data, size_t size);
  Image decode_ics8(int16_t id, uint32_t type = RESOURCE_TYPE_ics8);
  Image decode_ics8(std::shared_ptr<const Resource> res);
  static Image decode_ics8_without_alpha(const void* data, size_t size);
  Image decode_kcs8(int16_t id, uint32_t type = RESOURCE_TYPE_kcs8);
  Image decode_kcs8(std::shared_ptr<const Resource> res);
  static Image decode_kcs8_without_alpha(const void* data, size_t size);
  Image decode_icl4(int16_t id, uint32_t type = RESOURCE_TYPE_icl4);
  Image decode_icl4(std::shared_ptr<const Resource> res);
  static Image decode_icl4_without_alpha(const void* data, size_t size);
  Image decode_icm4(int16_t id, uint32_t type = RESOURCE_TYPE_icm4);
  Image decode_icm4(std::shared_ptr<const Resource> res);
  static Image decode_icm4_without_alpha(const void* data, size_t size);
  Image decode_ics4(int16_t id, uint32_t type = RESOURCE_TYPE_ics4);
  Image decode_ics4(std::shared_ptr<const Resource> res);
  static Image decode_ics4_without_alpha(const void* data, size_t size);
  Image decode_kcs4(int16_t id, uint32_t type = RESOURCE_TYPE_kcs4);
  Image decode_kcs4(std::shared_ptr<const Resource> res);
  static Image decode_kcs4_without_alpha(const void* data, size_t size);
  Image decode_ICON(int16_t id, uint32_t type = RESOURCE_TYPE_ICON);
  static Image decode_ICON(std::shared_ptr<const Resource> res);
  static Image decode_ICON(const void* data, size_t size);
  DecodedIconListResource decode_ICNN(int16_t id, uint32_t type = RESOURCE_TYPE_ICNN);
  static DecodedIconListResource decode_ICNN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_ICNN(const void* data, size_t size);
  DecodedIconListResource decode_icmN(int16_t id, uint32_t type = RESOURCE_TYPE_icmN);
  static DecodedIconListResource decode_icmN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_icmN(const void* data, size_t size);
  DecodedIconListResource decode_icsN(int16_t id, uint32_t type = RESOURCE_TYPE_icsN);
  static DecodedIconListResource decode_icsN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_icsN(const void* data, size_t size);
  DecodedIconListResource decode_kcsN(int16_t id, uint32_t type = RESOURCE_TYPE_kcsN);
  static DecodedIconListResource decode_kcsN(std::shared_ptr<const Resource> res);
  static DecodedIconListResource decode_kcsN(const void* data, size_t size);
  DecodedIconImagesResource decode_icns(int16_t id, uint32_t type = RESOURCE_TYPE_icns);
  static DecodedIconImagesResource decode_icns(std::shared_ptr<const Resource> res);
  static DecodedIconImagesResource decode_icns(const void* data, size_t size);
  DecodedPictResource decode_PICT(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  DecodedPictResource decode_PICT(std::shared_ptr<const Resource> res);
  DecodedPictResource decode_PICT_internal(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  DecodedPictResource decode_PICT_internal(std::shared_ptr<const Resource> res);
  Image decode_PICT_external(int16_t id, uint32_t type = RESOURCE_TYPE_PICT);
  static Image decode_PICT_external(std::shared_ptr<const Resource> res);
  static Image decode_PICT_external(const void* data, size_t size);
  std::vector<Color> decode_pltt(int16_t id, uint32_t type = RESOURCE_TYPE_pltt);
  static std::vector<Color> decode_pltt(std::shared_ptr<const Resource> res);
  static std::vector<Color> decode_pltt(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_clut(int16_t id, uint32_t type = RESOURCE_TYPE_clut);
  static std::vector<ColorTableEntry> decode_clut(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_clut(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_actb(int16_t id, uint32_t type = RESOURCE_TYPE_actb);
  static std::vector<ColorTableEntry> decode_actb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_actb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_cctb(int16_t id, uint32_t type = RESOURCE_TYPE_cctb);
  static std::vector<ColorTableEntry> decode_cctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_cctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_dctb(int16_t id, uint32_t type = RESOURCE_TYPE_dctb);
  static std::vector<ColorTableEntry> decode_dctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_dctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_fctb(int16_t id, uint32_t type = RESOURCE_TYPE_fctb);
  static std::vector<ColorTableEntry> decode_fctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_fctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_wctb(int16_t id, uint32_t type = RESOURCE_TYPE_wctb);
  static std::vector<ColorTableEntry> decode_wctb(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_wctb(const void* data, size_t size);
  std::vector<ColorTableEntry> decode_CTBL(int16_t id, uint32_t type = RESOURCE_TYPE_CTBL);
  static std::vector<ColorTableEntry> decode_CTBL(std::shared_ptr<const Resource> res);
  static std::vector<ColorTableEntry> decode_CTBL(const void* data, size_t size);

  // Sound resources
  // Note: return types may change here in the future to improve structuring and
  // to make it easier for callers of the library to use the returned data in
  // any way other than just saving it to WAV/MIDI files
  DecodedInstrumentResource decode_INST(int16_t id, uint32_t type = RESOURCE_TYPE_INST);
  DecodedInstrumentResource decode_INST(std::shared_ptr<const Resource> res);
  // Note: The SONG format depends on the resource index format, so there are no
  // static versions of this function.
  DecodedSongResource decode_SONG(int16_t id, uint32_t type = RESOURCE_TYPE_SONG);
  DecodedSongResource decode_SONG(std::shared_ptr<const Resource> res);
  DecodedSongResource decode_SONG(const void* data, size_t size);
  // If metadata_only is true, the .data field in the returned struct will be
  // empty. This saves time when generating SONG JSONs, for example.
  DecodedSoundResource decode_snd(int16_t id, uint32_t type = RESOURCE_TYPE_snd, bool metadata_only = false);
  DecodedSoundResource decode_snd(std::shared_ptr<const Resource> res, bool metadata_only = false);
  DecodedSoundResource decode_snd(const void* data, size_t size, bool metadata_only = false);
  DecodedSoundResource decode_csnd(int16_t id, uint32_t type = RESOURCE_TYPE_csnd, bool metadata_only = false);
  DecodedSoundResource decode_csnd(std::shared_ptr<const Resource> res, bool metadata_only = false);
  DecodedSoundResource decode_csnd(const void* data, size_t size, bool metadata_only = false);
  DecodedSoundResource decode_esnd(int16_t id, uint32_t type = RESOURCE_TYPE_esnd, bool metadata_only = false);
  DecodedSoundResource decode_esnd(std::shared_ptr<const Resource> res, bool metadata_only = false);
  DecodedSoundResource decode_esnd(const void* data, size_t size, bool metadata_only = false);
  DecodedSoundResource decode_ESnd(int16_t id, uint32_t type = RESOURCE_TYPE_ESnd, bool metadata_only = false);
  DecodedSoundResource decode_ESnd(std::shared_ptr<const Resource> res, bool metadata_only = false);
  DecodedSoundResource decode_ESnd(const void* data, size_t size, bool metadata_only = false);
  DecodedSoundResource decode_Ysnd(int16_t id, uint32_t type, bool metadata_only = false);
  DecodedSoundResource decode_Ysnd(std::shared_ptr<const Resource> res, bool metadata_only = false);
  DecodedSoundResource decode_Ysnd(const void* vdata, size_t size, bool metadata_only = false);
  // These function return a string containing a raw WAV file.
  std::string decode_SMSD(int16_t id, uint32_t type = RESOURCE_TYPE_SMSD);
  static std::string decode_SMSD(std::shared_ptr<const Resource> res);
  static std::string decode_SMSD(const void* data, size_t size);
  std::string decode_SOUN(int16_t id, uint32_t type = RESOURCE_TYPE_SOUN);
  static std::string decode_SOUN(std::shared_ptr<const Resource> res);
  static std::string decode_SOUN(const void* data, size_t size);
  // The strings returned by these functions contain raw MIDI files
  std::string decode_cmid(int16_t id, uint32_t type = RESOURCE_TYPE_cmid);
  static std::string decode_cmid(std::shared_ptr<const Resource> res);
  static std::string decode_cmid(const void* data, size_t size);
  std::string decode_emid(int16_t id, uint32_t type = RESOURCE_TYPE_emid);
  static std::string decode_emid(std::shared_ptr<const Resource> res);
  static std::string decode_emid(const void* data, size_t size);
  std::string decode_ecmi(int16_t id, uint32_t type = RESOURCE_TYPE_ecmi);
  static std::string decode_ecmi(std::shared_ptr<const Resource> res);
  static std::string decode_ecmi(const void* data, size_t size);
  std::string decode_Tune(int16_t id, uint32_t type = RESOURCE_TYPE_Tune);
  static std::string decode_Tune(std::shared_ptr<const Resource> res);
  static std::string decode_Tune(const void* data, size_t size);

  // Text resources
  DecodedString decode_STR(int16_t id, uint32_t type = RESOURCE_TYPE_STR);
  static DecodedString decode_STR(std::shared_ptr<const Resource> res);
  static DecodedString decode_STR(const void* data, size_t size);
  std::string decode_card(int16_t id, uint32_t type = RESOURCE_TYPE_STR);
  static std::string decode_card(std::shared_ptr<const Resource> res);
  static std::string decode_card(const void* data, size_t size);
  DecodedStringSequence decode_STRN(int16_t id, uint32_t type = RESOURCE_TYPE_STRN);
  static DecodedStringSequence decode_STRN(std::shared_ptr<const Resource> res);
  static DecodedStringSequence decode_STRN(const void* data, size_t size);
  std::string decode_TEXT(int16_t id, uint32_t type = RESOURCE_TYPE_TEXT);
  static std::string decode_TEXT(std::shared_ptr<const Resource> res);
  static std::string decode_TEXT(const void* data, size_t size);
  std::string decode_styl(int16_t id, uint32_t type = RESOURCE_TYPE_styl);
  std::string decode_styl(std::shared_ptr<const Resource> res);

  // Font resources
  DecodedFontResource decode_FONT(int16_t id, uint32_t type = RESOURCE_TYPE_FONT);
  DecodedFontResource decode_FONT(std::shared_ptr<const Resource> res);
  DecodedFontResource decode_NFNT(int16_t id, uint32_t type = RESOURCE_TYPE_NFNT);
  DecodedFontResource decode_NFNT(std::shared_ptr<const Resource> res);
  std::vector<DecodedFontInfo> decode_finf(int16_t id, uint32_t type = RESOURCE_TYPE_finf);
  static std::vector<DecodedFontInfo> decode_finf(std::shared_ptr<const Resource> res);
  static std::vector<DecodedFontInfo> decode_finf(const void* data, size_t size);

private:
  IndexFormat format;
  // Note: It's important that this is not an unordered_map because we expect
  // all_resources to always return resources of the same type contiguously
  std::map<uint64_t, std::shared_ptr<Resource>> key_to_resource;
  std::multimap<std::string, std::shared_ptr<Resource>> name_to_resource;
  std::unordered_map<int16_t, std::shared_ptr<Resource>> system_dcmp_cache;

  DecodedInstrumentResource decode_INST_recursive(
      std::shared_ptr<const Resource> res,
      std::unordered_set<int16_t>& ids_in_progress);

  void add_name_index_entry(std::shared_ptr<Resource> res);
  void delete_name_index_entry(std::shared_ptr<Resource> res);

  static uint64_t make_resource_key(uint32_t type, int16_t id);
  static uint32_t type_from_resource_key(uint64_t key);
  static int16_t id_from_resource_key(uint64_t key);

  static const std::vector<std::shared_ptr<TemplateEntry>>& get_system_template(
      uint32_t type);
};



